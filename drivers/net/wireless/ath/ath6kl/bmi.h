/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef BMI_H
#define BMI_H

/*
 * Bootloader Messaging Interface (BMI)
 *
 * BMI is a very simple messaging interface used during initialization
 * to read memory, write memory, execute code, and to define an
 * application entry PC.
 *
 * It is used to download an application to ATH6KL, to provide
 * patches to code that is already resident on ATH6KL, and generally
 * to examine and modify state.  The Host has an opportunity to use
 * BMI only once during bootup.  Once the Host issues a BMI_DONE
 * command, this opportunity ends.
 *
 * The Host writes BMI requests to mailbox0, and reads BMI responses
 * from mailbox0.   BMI requests all begin with a command
 * (see below for specific commands), and are followed by
 * command-specific data.
 *
 * Flow control:
 * The Host can only issue a command once the Target gives it a
 * "BMI Command Credit", using ATH6KL Counter #4.  As soon as the
 * Target has completed a command, it issues another BMI Command
 * Credit (so the Host can issue the next command).
 *
 * BMI handles all required Target-side cache flushing.
 */

#define MAX_BMI_CMDBUF_SZ (BMI_DATASZ_MAX + \
			   (sizeof(u32) * 3 /* cmd + addr + len */))

/* Maximum data size used for BMI transfers */
#define BMI_DATASZ_MAX                      256

/* BMI Commands */

#define BMI_NO_COMMAND                      0

#define BMI_DONE                            1
/*
 * Semantics: Host is done using BMI
 * Request format:
 *    u32 command (BMI_DONE)
 * Response format: none
 */

#define BMI_READ_MEMORY                     2
/*
 * Semantics: Host reads ATH6KL memory
 * Request format:
 *    u32 command (BMI_READ_MEMORY)
 *    u32 address
 *    u32 length, at most BMI_DATASZ_MAX
 * Response format:
 *    u8 data[length]
 */

#define BMI_WRITE_MEMORY                    3
/*
 * Semantics: Host writes ATH6KL memory
 * Request format:
 *    u32 command (BMI_WRITE_MEMORY)
 *    u32 address
 *    u32 length, at most BMI_DATASZ_MAX
 *    u8 data[length]
 * Response format: none
 */

#define BMI_EXECUTE                         4
/*
 * Semantics: Causes ATH6KL to execute code
 * Request format:
 *    u32 command (BMI_EXECUTE)
 *    u32 address
 *    u32 parameter
 * Response format:
 *    u32 return value
 */

#define BMI_SET_APP_START                   5
/*
 * Semantics: Set Target application starting address
 * Request format:
 *    u32 command (BMI_SET_APP_START)
 *    u32 address
 * Response format: none
 */

#define BMI_READ_SOC_REGISTER               6
/*
 * Semantics: Read a 32-bit Target SOC register.
 * Request format:
 *    u32 command (BMI_READ_REGISTER)
 *    u32 address
 * Response format:
 *    u32 value
 */

#define BMI_WRITE_SOC_REGISTER              7
/*
 * Semantics: Write a 32-bit Target SOC register.
 * Request format:
 *    u32 command (BMI_WRITE_REGISTER)
 *    u32 address
 *    u32 value
 *
 * Response format: none
 */

#define BMI_GET_TARGET_ID                  8
#define BMI_GET_TARGET_INFO                8
/*
 * Semantics: Fetch the 4-byte Target information
 * Request format:
 *    u32 command (BMI_GET_TARGET_ID/INFO)
 * Response format1 (old firmware):
 *    u32 TargetVersionID
 * Response format2 (newer firmware):
 *    u32 TARGET_VERSION_SENTINAL
 *    struct bmi_target_info;
 */

#define TARGET_VERSION_SENTINAL 0xffffffff
#define TARGET_TYPE_AR6003      3
#define TARGET_TYPE_AR6004      5
#define BMI_ROMPATCH_INSTALL               9
/*
 * Semantics: Install a ROM Patch.
 * Request format:
 *    u32 command (BMI_ROMPATCH_INSTALL)
 *    u32 Target ROM Address
 *    u32 Target RAM Address or Value (depending on Target Type)
 *    u32 Size, in bytes
 *    u32 Activate? 1-->activate;
 *                            0-->install but do not activate
 * Response format:
 *    u32 PatchID
 */

#define BMI_ROMPATCH_UNINSTALL             10
/*
 * Semantics: Uninstall a previously-installed ROM Patch,
 * automatically deactivating, if necessary.
 * Request format:
 *    u32 command (BMI_ROMPATCH_UNINSTALL)
 *    u32 PatchID
 *
 * Response format: none
 */

#define BMI_ROMPATCH_ACTIVATE              11
/*
 * Semantics: Activate a list of previously-installed ROM Patches.
 * Request format:
 *    u32 command (BMI_ROMPATCH_ACTIVATE)
 *    u32 rompatch_count
 *    u32 PatchID[rompatch_count]
 *
 * Response format: none
 */

#define BMI_ROMPATCH_DEACTIVATE            12
/*
 * Semantics: Deactivate a list of active ROM Patches.
 * Request format:
 *    u32 command (BMI_ROMPATCH_DEACTIVATE)
 *    u32 rompatch_count
 *    u32 PatchID[rompatch_count]
 *
 * Response format: none
 */


#define BMI_LZ_STREAM_START                13
/*
 * Semantics: Begin an LZ-compressed stream of input
 * which is to be uncompressed by the Target to an
 * output buffer at address.  The output buffer must
 * be sufficiently large to hold the uncompressed
 * output from the compressed input stream.  This BMI
 * command should be followed by a series of 1 or more
 * BMI_LZ_DATA commands.
 *    u32 command (BMI_LZ_STREAM_START)
 *    u32 address
 * Note: Not supported on all versions of ROM firmware.
 */

#define BMI_LZ_DATA                        14
/*
 * Semantics: Host writes ATH6KL memory with LZ-compressed
 * data which is uncompressed by the Target.  This command
 * must be preceded by a BMI_LZ_STREAM_START command. A series
 * of BMI_LZ_DATA commands are considered part of a single
 * input stream until another BMI_LZ_STREAM_START is issued.
 * Request format:
 *    u32 command (BMI_LZ_DATA)
 *    u32 length (of compressed data),
 *                  at most BMI_DATASZ_MAX
 *    u8 CompressedData[length]
 * Response format: none
 * Note: Not supported on all versions of ROM firmware.
 */

#define BMI_COMMUNICATION_TIMEOUT       1000 /* in msec */

struct ath6kl;
struct ath6kl_bmi_target_info {
	__le32 byte_count;   /* size of this structure */
	__le32 version;      /* target version id */
	__le32 type;         /* target type */
} __packed;

int ath6kl_bmi_init(struct ath6kl *ar);
void ath6kl_bmi_cleanup(struct ath6kl *ar);
void ath6kl_bmi_reset(struct ath6kl *ar);

int ath6kl_bmi_done(struct ath6kl *ar);
int ath6kl_bmi_get_target_info(struct ath6kl *ar,
			       struct ath6kl_bmi_target_info *targ_info);
int ath6kl_bmi_read(struct ath6kl *ar, u32 addr, u8 *buf, u32 len);
int ath6kl_bmi_write(struct ath6kl *ar, u32 addr, u8 *buf, u32 len);
int ath6kl_bmi_execute(struct ath6kl *ar,
		       u32 addr, u32 *param);
int ath6kl_bmi_set_app_start(struct ath6kl *ar,
			     u32 addr);
int ath6kl_bmi_reg_read(struct ath6kl *ar, u32 addr, u32 *param);
int ath6kl_bmi_reg_write(struct ath6kl *ar, u32 addr, u32 param);
int ath6kl_bmi_lz_data(struct ath6kl *ar,
		       u8 *buf, u32 len);
int ath6kl_bmi_lz_stream_start(struct ath6kl *ar,
			       u32 addr);
int ath6kl_bmi_fast_download(struct ath6kl *ar,
			     u32 addr, u8 *buf, u32 len);
#endif
