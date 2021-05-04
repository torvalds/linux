// SPDX-License-Identifier: GPL-2.0+
/*
 * SSH message parser.
 *
 * Copyright (C) 2019-2020 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <asm/unaligned.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/types.h>

#include <linux/surface_aggregator/serial_hub.h>
#include "ssh_parser.h"

/**
 * sshp_validate_crc() - Validate a CRC in raw message data.
 * @src: The span of data over which the CRC should be computed.
 * @crc: The pointer to the expected u16 CRC value.
 *
 * Computes the CRC of the provided data span (@src), compares it to the CRC
 * stored at the given address (@crc), and returns the result of this
 * comparison, i.e. %true if equal. This function is intended to run on raw
 * input/message data.
 *
 * Return: Returns %true if the computed CRC matches the stored CRC, %false
 * otherwise.
 */
static bool sshp_validate_crc(const struct ssam_span *src, const u8 *crc)
{
	u16 actual = ssh_crc(src->ptr, src->len);
	u16 expected = get_unaligned_le16(crc);

	return actual == expected;
}

/**
 * sshp_starts_with_syn() - Check if the given data starts with SSH SYN bytes.
 * @src: The data span to check the start of.
 */
static bool sshp_starts_with_syn(const struct ssam_span *src)
{
	return src->len >= 2 && get_unaligned_le16(src->ptr) == SSH_MSG_SYN;
}

/**
 * sshp_find_syn() - Find SSH SYN bytes in the given data span.
 * @src: The data span to search in.
 * @rem: The span (output) indicating the remaining data, starting with SSH
 *       SYN bytes, if found.
 *
 * Search for SSH SYN bytes in the given source span. If found, set the @rem
 * span to the remaining data, starting with the first SYN bytes and capped by
 * the source span length, and return %true. This function does not copy any
 * data, but rather only sets pointers to the respective start addresses and
 * length values.
 *
 * If no SSH SYN bytes could be found, set the @rem span to the zero-length
 * span at the end of the source span and return %false.
 *
 * If partial SSH SYN bytes could be found at the end of the source span, set
 * the @rem span to cover these partial SYN bytes, capped by the end of the
 * source span, and return %false. This function should then be re-run once
 * more data is available.
 *
 * Return: Returns %true if a complete SSH SYN sequence could be found,
 * %false otherwise.
 */
bool sshp_find_syn(const struct ssam_span *src, struct ssam_span *rem)
{
	size_t i;

	for (i = 0; i < src->len - 1; i++) {
		if (likely(get_unaligned_le16(src->ptr + i) == SSH_MSG_SYN)) {
			rem->ptr = src->ptr + i;
			rem->len = src->len - i;
			return true;
		}
	}

	if (unlikely(src->ptr[src->len - 1] == (SSH_MSG_SYN & 0xff))) {
		rem->ptr = src->ptr + src->len - 1;
		rem->len = 1;
		return false;
	}

	rem->ptr = src->ptr + src->len;
	rem->len = 0;
	return false;
}

/**
 * sshp_parse_frame() - Parse SSH frame.
 * @dev: The device used for logging.
 * @source: The source to parse from.
 * @frame: The parsed frame (output).
 * @payload: The parsed payload (output).
 * @maxlen: The maximum supported message length.
 *
 * Parses and validates a SSH frame, including its payload, from the given
 * source. Sets the provided @frame pointer to the start of the frame and
 * writes the limits of the frame payload to the provided @payload span
 * pointer.
 *
 * This function does not copy any data, but rather only validates the message
 * data and sets pointers (and length values) to indicate the respective parts.
 *
 * If no complete SSH frame could be found, the frame pointer will be set to
 * the %NULL pointer and the payload span will be set to the null span (start
 * pointer %NULL, size zero).
 *
 * Return: Returns zero on success or if the frame is incomplete, %-ENOMSG if
 * the start of the message is invalid, %-EBADMSG if any (frame-header or
 * payload) CRC is invalid, or %-EMSGSIZE if the SSH message is bigger than
 * the maximum message length specified in the @maxlen parameter.
 */
int sshp_parse_frame(const struct device *dev, const struct ssam_span *source,
		     struct ssh_frame **frame, struct ssam_span *payload,
		     size_t maxlen)
{
	struct ssam_span sf;
	struct ssam_span sp;

	/* Initialize output. */
	*frame = NULL;
	payload->ptr = NULL;
	payload->len = 0;

	if (!sshp_starts_with_syn(source)) {
		dev_warn(dev, "rx: parser: invalid start of frame\n");
		return -ENOMSG;
	}

	/* Check for minimum packet length. */
	if (unlikely(source->len < SSH_MESSAGE_LENGTH(0))) {
		dev_dbg(dev, "rx: parser: not enough data for frame\n");
		return 0;
	}

	/* Pin down frame. */
	sf.ptr = source->ptr + sizeof(u16);
	sf.len = sizeof(struct ssh_frame);

	/* Validate frame CRC. */
	if (unlikely(!sshp_validate_crc(&sf, sf.ptr + sf.len))) {
		dev_warn(dev, "rx: parser: invalid frame CRC\n");
		return -EBADMSG;
	}

	/* Ensure packet does not exceed maximum length. */
	sp.len = get_unaligned_le16(&((struct ssh_frame *)sf.ptr)->len);
	if (unlikely(SSH_MESSAGE_LENGTH(sp.len) > maxlen)) {
		dev_warn(dev, "rx: parser: frame too large: %llu bytes\n",
			 SSH_MESSAGE_LENGTH(sp.len));
		return -EMSGSIZE;
	}

	/* Pin down payload. */
	sp.ptr = sf.ptr + sf.len + sizeof(u16);

	/* Check for frame + payload length. */
	if (source->len < SSH_MESSAGE_LENGTH(sp.len)) {
		dev_dbg(dev, "rx: parser: not enough data for payload\n");
		return 0;
	}

	/* Validate payload CRC. */
	if (unlikely(!sshp_validate_crc(&sp, sp.ptr + sp.len))) {
		dev_warn(dev, "rx: parser: invalid payload CRC\n");
		return -EBADMSG;
	}

	*frame = (struct ssh_frame *)sf.ptr;
	*payload = sp;

	dev_dbg(dev, "rx: parser: valid frame found (type: %#04x, len: %u)\n",
		(*frame)->type, (*frame)->len);

	return 0;
}

/**
 * sshp_parse_command() - Parse SSH command frame payload.
 * @dev: The device used for logging.
 * @source: The source to parse from.
 * @command: The parsed command (output).
 * @command_data: The parsed command data/payload (output).
 *
 * Parses and validates a SSH command frame payload. Sets the @command pointer
 * to the command header and the @command_data span to the command data (i.e.
 * payload of the command). This will result in a zero-length span if the
 * command does not have any associated data/payload. This function does not
 * check the frame-payload-type field, which should be checked by the caller
 * before calling this function.
 *
 * The @source parameter should be the complete frame payload, e.g. returned
 * by the sshp_parse_frame() command.
 *
 * This function does not copy any data, but rather only validates the frame
 * payload data and sets pointers (and length values) to indicate the
 * respective parts.
 *
 * Return: Returns zero on success or %-ENOMSG if @source does not represent a
 * valid command-type frame payload, i.e. is too short.
 */
int sshp_parse_command(const struct device *dev, const struct ssam_span *source,
		       struct ssh_command **command,
		       struct ssam_span *command_data)
{
	/* Check for minimum length. */
	if (unlikely(source->len < sizeof(struct ssh_command))) {
		*command = NULL;
		command_data->ptr = NULL;
		command_data->len = 0;

		dev_err(dev, "rx: parser: command payload is too short\n");
		return -ENOMSG;
	}

	*command = (struct ssh_command *)source->ptr;
	command_data->ptr = source->ptr + sizeof(struct ssh_command);
	command_data->len = source->len - sizeof(struct ssh_command);

	dev_dbg(dev, "rx: parser: valid command found (tc: %#04x, cid: %#04x)\n",
		(*command)->tc, (*command)->cid);

	return 0;
}
