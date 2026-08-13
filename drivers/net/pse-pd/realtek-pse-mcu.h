/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _REALTEK_PSE_MCU_H
#define _REALTEK_PSE_MCU_H

#include <linux/mutex.h>
#include <linux/pse-pd/pse.h>
#include <linux/types.h>

/*
 * Time the MCU itself needs between accepting a request and having a
 * response ready. These are properties of the MCU firmware, not of the
 * underlying transport: the core paces transactions by RTPSE_MCU_RESPONSE_MS
 * and both transports size their per-transaction recv ceiling from
 * RTPSE_MCU_RESPONSE_MAX_MS, since some commands are documented as
 * needing up to ~1s to produce a reply.
 */
#define RTPSE_MCU_RESPONSE_MS			25
#define RTPSE_MCU_RESPONSE_MAX_MS		1000

/*
 * Total time to keep retrying the first MCU read at probe, and the pause
 * between attempts. Right after reset-gpios is deasserted the MCU may not
 * answer on the bus yet; give it a bounded window to come up before
 * declaring the probe failed.
 */
#define RTPSE_MCU_BOOT_TIMEOUT_MS		3000
#define RTPSE_MCU_BOOT_RETRY_MS			100

#define RTPSE_MCU_MSG_SIZE			12

struct rtpse_mcu_msg {
	u8 opcode;
	u8 seq_num;
	u8 payload[9];
	u8 checksum;
} __packed;

/*
 * MCU status opcodes (seen on the Gen1 dialect; Gen2 never emits them).
 * INCOMPLETE/BAD_CSUM are terminal; NOT_READY is transient.
 */
#define RTPSE_MCU_OPCODE_INCOMPLETE		0xfd	/* -EBADE   */
#define RTPSE_MCU_OPCODE_BAD_CSUM		0xfe	/* -EBADMSG */
#define RTPSE_MCU_OPCODE_NOT_READY		0xff	/* -EAGAIN  */

/*
 * A polling transport can stop here: the reply to this request (opcode and
 * seq_num) or a terminal error. The seq_num rejects a stale normal reply; the
 * terminal errors match unconditionally, as a request the MCU couldn't parse
 * carries no seq_num to correlate against.
 */
static inline bool rtpse_mcu_resp_is_final(const struct rtpse_mcu_msg *req,
					   const struct rtpse_mcu_msg *resp)
{
	return (resp->opcode == req->opcode && resp->seq_num == req->seq_num) ||
	       resp->opcode == RTPSE_MCU_OPCODE_INCOMPLETE ||
	       resp->opcode == RTPSE_MCU_OPCODE_BAD_CSUM;
}

/* Opaque to transports; defined in realtek-pse-mcu-core.c. */
struct rtpse_mcu_dialect;
struct rtpse_mcu_chip_info;
struct rtpse_mcu_ctrl;

/* Per-compatible match data (the of_match .data). */
struct rtpse_mcu_match_data {
	const struct rtpse_mcu_dialect *dialect;
	bool native_i2c;	/* raw-I2C framing (vs SMBus); I2C transport only */
};

struct rtpse_mcu_transport_ops {
	int (*send)(struct rtpse_mcu_ctrl *pse, const struct rtpse_mcu_msg *req);
	int (*recv)(struct rtpse_mcu_ctrl *pse, const struct rtpse_mcu_msg *req,
		    struct rtpse_mcu_msg *resp);
};

struct rtpse_mcu_ctrl {
	struct device *dev;
	struct pse_controller_dev pcdev;
	struct mutex mutex;	/* serializes MCU request/response transactions */
	const struct rtpse_mcu_dialect *dialect;
	const struct rtpse_mcu_chip_info *chip;
	const struct rtpse_mcu_transport_ops *transport;
	u8 seq;			/* rolling request seq_num, echoed by the MCU */
};

int rtpse_mcu_register(struct rtpse_mcu_ctrl *pse);

extern const struct rtpse_mcu_match_data rtpse_mcu_gen1_data;
extern const struct rtpse_mcu_match_data rtpse_mcu_gen2_data;
extern const struct rtpse_mcu_match_data rtpse_mcu_gen2_i2c_data;

#endif
