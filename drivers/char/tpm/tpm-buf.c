// SPDX-License-Identifier: GPL-2.0
/*
 * Handling of TPM command and other buffers.
 */

#include <linux/tpm_command.h>
#include <linux/module.h>
#include <linux/tpm.h>

/**
 * tpm_buf_init() - Allocate and initialize a TPM command
 * @buf:	A &tpm_buf
 * @tag:	TPM_TAG_RQU_COMMAND, TPM2_ST_NO_SESSIONS or TPM2_ST_SESSIONS
 * @ordinal:	A command ordinal
 *
 * Return: 0 or -ENOMEM
 */
int tpm_buf_init(struct tpm_buf *buf, u16 tag, u32 ordinal)
{
	buf->data = (u8 *)__get_free_page(GFP_KERNEL);
	if (!buf->data)
		return -ENOMEM;

	tpm_buf_reset(buf, tag, ordinal);
	return 0;
}
EXPORT_SYMBOL_GPL(tpm_buf_init);

/**
 * tpm_buf_reset() - Initialize a TPM command
 * @buf:	A &tpm_buf
 * @tag:	TPM_TAG_RQU_COMMAND, TPM2_ST_NO_SESSIONS or TPM2_ST_SESSIONS
 * @ordinal:	A command ordinal
 */
void tpm_buf_reset(struct tpm_buf *buf, u16 tag, u32 ordinal)
{
	struct tpm_header *head = (struct tpm_header *)buf->data;

	WARN_ON(tag != TPM_TAG_RQU_COMMAND && tag != TPM2_ST_NO_SESSIONS &&
		tag != TPM2_ST_SESSIONS && tag != 0);

	buf->flags = 0;
	buf->length = sizeof(*head);
	head->tag = cpu_to_be16(tag);
	head->length = cpu_to_be32(sizeof(*head));
	head->ordinal = cpu_to_be32(ordinal);
	buf->handles = 0;
}
EXPORT_SYMBOL_GPL(tpm_buf_reset);

/**
 * tpm_buf_init_sized() - Allocate and initialize a sized (TPM2B) buffer
 * @buf:	A @tpm_buf
 *
 * Return: 0 or -ENOMEM
 */
int tpm_buf_init_sized(struct tpm_buf *buf)
{
	buf->data = (u8 *)__get_free_page(GFP_KERNEL);
	if (!buf->data)
		return -ENOMEM;

	tpm_buf_reset_sized(buf);
	return 0;
}
EXPORT_SYMBOL_GPL(tpm_buf_init_sized);

/**
 * tpm_buf_reset_sized() - Initialize a sized buffer
 * @buf:	A &tpm_buf
 */
void tpm_buf_reset_sized(struct tpm_buf *buf)
{
	buf->flags = TPM_BUF_TPM2B;
	buf->length = 2;
	buf->data[0] = 0;
	buf->data[1] = 0;
}
EXPORT_SYMBOL_GPL(tpm_buf_reset_sized);

void tpm_buf_destroy(struct tpm_buf *buf)
{
	free_page((unsigned long)buf->data);
}
EXPORT_SYMBOL_GPL(tpm_buf_destroy);

/**
 * tpm_buf_length() - Return the number of bytes consumed by the data
 * @buf:	A &tpm_buf
 *
 * Return: The number of bytes consumed by the buffer
 */
u32 tpm_buf_length(struct tpm_buf *buf)
{
	return buf->length;
}
EXPORT_SYMBOL_GPL(tpm_buf_length);

/**
 * tpm_buf_append() - Append data to an initialized buffer
 * @buf:	A &tpm_buf
 * @new_data:	A data blob
 * @new_length:	Size of the appended data
 */
void tpm_buf_append(struct tpm_buf *buf, const u8 *new_data, u16 new_length)
{
	/* Return silently if overflow has already happened. */
	if (buf->flags & TPM_BUF_OVERFLOW)
		return;

	if ((buf->length + new_length) > PAGE_SIZE) {
		WARN(1, "tpm_buf: write overflow\n");
		buf->flags |= TPM_BUF_OVERFLOW;
		return;
	}

	memcpy(&buf->data[buf->length], new_data, new_length);
	buf->length += new_length;

	if (buf->flags & TPM_BUF_TPM2B)
		((__be16 *)buf->data)[0] = cpu_to_be16(buf->length - 2);
	else
		((struct tpm_header *)buf->data)->length = cpu_to_be32(buf->length);
}
EXPORT_SYMBOL_GPL(tpm_buf_append);

void tpm_buf_append_u8(struct tpm_buf *buf, const u8 value)
{
	tpm_buf_append(buf, &value, 1);
}
EXPORT_SYMBOL_GPL(tpm_buf_append_u8);

void tpm_buf_append_u16(struct tpm_buf *buf, const u16 value)
{
	__be16 value2 = cpu_to_be16(value);

	tpm_buf_append(buf, (u8 *)&value2, 2);
}
EXPORT_SYMBOL_GPL(tpm_buf_append_u16);

void tpm_buf_append_u32(struct tpm_buf *buf, const u32 value)
{
	__be32 value2 = cpu_to_be32(value);

	tpm_buf_append(buf, (u8 *)&value2, 4);
}
EXPORT_SYMBOL_GPL(tpm_buf_append_u32);

/**
 * tpm_buf_read() - Read from a TPM buffer
 * @buf:	&tpm_buf instance
 * @offset:	offset within the buffer
 * @count:	the number of bytes to read
 * @output:	the output buffer
 */
static void tpm_buf_read(struct tpm_buf *buf, off_t *offset, size_t count, void *output)
{
	off_t next_offset;

	/* Return silently if overflow has already happened. */
	if (buf->flags & TPM_BUF_BOUNDARY_ERROR)
		return;

	next_offset = *offset + count;
	if (next_offset > buf->length) {
		WARN(1, "tpm_buf: read out of boundary\n");
		buf->flags |= TPM_BUF_BOUNDARY_ERROR;
		return;
	}

	memcpy(output, &buf->data[*offset], count);
	*offset = next_offset;
}

/**
 * tpm_buf_read_u8() - Read 8-bit word from a TPM buffer
 * @buf:	&tpm_buf instance
 * @offset:	offset within the buffer
 *
 * Return: next 8-bit word
 */
u8 tpm_buf_read_u8(struct tpm_buf *buf, off_t *offset)
{
	u8 value;

	tpm_buf_read(buf, offset, sizeof(value), &value);

	return value;
}
EXPORT_SYMBOL_GPL(tpm_buf_read_u8);

/**
 * tpm_buf_read_u16() - Read 16-bit word from a TPM buffer
 * @buf:	&tpm_buf instance
 * @offset:	offset within the buffer
 *
 * Return: next 16-bit word
 */
u16 tpm_buf_read_u16(struct tpm_buf *buf, off_t *offset)
{
	u16 value;

	tpm_buf_read(buf, offset, sizeof(value), &value);

	return be16_to_cpu(value);
}
EXPORT_SYMBOL_GPL(tpm_buf_read_u16);

/**
 * tpm_buf_read_u32() - Read 32-bit word from a TPM buffer
 * @buf:	&tpm_buf instance
 * @offset:	offset within the buffer
 *
 * Return: next 32-bit word
 */
u32 tpm_buf_read_u32(struct tpm_buf *buf, off_t *offset)
{
	u32 value;

	tpm_buf_read(buf, offset, sizeof(value), &value);

	return be32_to_cpu(value);
}
EXPORT_SYMBOL_GPL(tpm_buf_read_u32);

static u16 tpm_buf_tag(struct tpm_buf *buf)
{
	struct tpm_header *head = (struct tpm_header *)buf->data;

	return be16_to_cpu(head->tag);
}

/**
 * tpm_buf_parameters - return the TPM response parameters area of the tpm_buf
 * @buf: tpm_buf to use
 *
 * Where the parameters are located depends on the tag of a TPM
 * command (it's immediately after the header for TPM_ST_NO_SESSIONS
 * or 4 bytes after for TPM_ST_SESSIONS). Evaluate this and return a
 * pointer to the first byte of the parameters area.
 *
 * @return: pointer to parameters area
 */
u8 *tpm_buf_parameters(struct tpm_buf *buf)
{
	int offset = TPM_HEADER_SIZE;

	if (tpm_buf_tag(buf) == TPM2_ST_SESSIONS)
		offset += 4;

	return &buf->data[offset];
}
