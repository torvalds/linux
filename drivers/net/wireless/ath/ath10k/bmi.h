/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

#ifndef _BMI_H_
#define _BMI_H_

#include "core.h"

/*
 * Bootloader Messaging Interface (BMI)
 *
 * BMI is a very simple messaging interface used during initialization
 * to read memory, write memory, execute code, and to define an
 * application entry PC.
 *
 * It is used to download an application to QCA988x, to provide
 * patches to code that is already resident on QCA988x, and generally
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
 * "BMI Command Credit", using AR8K Counter #4.  As soon as the
 * Target has completed a command, it issues another BMI Command
 * Credit (so the Host can issue the next command).
 *
 * BMI handles all required Target-side cache flushing.
 */

/* Maximum data size used for BMI transfers */
#define BMI_MAX_DATA_SIZE	256

/* len = cmd + addr + length */
#define BMI_MAX_CMDBUF_SIZE (BMI_MAX_DATA_SIZE + \
			sizeof(u32) + \
			sizeof(u32) + \
			sizeof(u32))

/* BMI Commands */

enum bmi_cmd_id {
	BMI_NO_COMMAND          = 0,
	BMI_DONE                = 1,
	BMI_READ_MEMORY         = 2,
	BMI_WRITE_MEMORY        = 3,
	BMI_EXECUTE             = 4,
	BMI_SET_APP_START       = 5,
	BMI_READ_SOC_REGISTER   = 6,
	BMI_READ_SOC_WORD       = 6,
	BMI_WRITE_SOC_REGISTER  = 7,
	BMI_WRITE_SOC_WORD      = 7,
	BMI_GET_TARGET_ID       = 8,
	BMI_GET_TARGET_INFO     = 8,
	BMI_ROMPATCH_INSTALL    = 9,
	BMI_ROMPATCH_UNINSTALL  = 10,
	BMI_ROMPATCH_ACTIVATE   = 11,
	BMI_ROMPATCH_DEACTIVATE = 12,
	BMI_LZ_STREAM_START     = 13, /* should be followed by LZ_DATA */
	BMI_LZ_DATA             = 14,
	BMI_NVRAM_PROCESS       = 15,
};

#define BMI_NVRAM_SEG_NAME_SZ 16

#define BMI_PARAM_GET_EEPROM_BOARD_ID 0x10

#define ATH10K_BMI_BOARD_ID_FROM_OTP_MASK   0x7c00
#define ATH10K_BMI_BOARD_ID_FROM_OTP_LSB    10

#define ATH10K_BMI_CHIP_ID_FROM_OTP_MASK    0x18000
#define ATH10K_BMI_CHIP_ID_FROM_OTP_LSB     15

#define ATH10K_BMI_BOARD_ID_STATUS_MASK 0xff

struct bmi_cmd {
	__le32 id; /* enum bmi_cmd_id */
	union {
		struct {
		} done;
		struct {
			__le32 addr;
			__le32 len;
		} read_mem;
		struct {
			__le32 addr;
			__le32 len;
			u8 payload[0];
		} write_mem;
		struct {
			__le32 addr;
			__le32 param;
		} execute;
		struct {
			__le32 addr;
		} set_app_start;
		struct {
			__le32 addr;
		} read_soc_reg;
		struct {
			__le32 addr;
			__le32 value;
		} write_soc_reg;
		struct {
		} get_target_info;
		struct {
			__le32 rom_addr;
			__le32 ram_addr; /* or value */
			__le32 size;
			__le32 activate; /* 0=install, but dont activate */
		} rompatch_install;
		struct {
			__le32 patch_id;
		} rompatch_uninstall;
		struct {
			__le32 count;
			__le32 patch_ids[0]; /* length of @count */
		} rompatch_activate;
		struct {
			__le32 count;
			__le32 patch_ids[0]; /* length of @count */
		} rompatch_deactivate;
		struct {
			__le32 addr;
		} lz_start;
		struct {
			__le32 len; /* max BMI_MAX_DATA_SIZE */
			u8 payload[0]; /* length of @len */
		} lz_data;
		struct {
			u8 name[BMI_NVRAM_SEG_NAME_SZ];
		} nvram_process;
		u8 payload[BMI_MAX_CMDBUF_SIZE];
	};
} __packed;

union bmi_resp {
	struct {
		u8 payload[0];
	} read_mem;
	struct {
		__le32 result;
	} execute;
	struct {
		__le32 value;
	} read_soc_reg;
	struct {
		__le32 len;
		__le32 version;
		__le32 type;
	} get_target_info;
	struct {
		__le32 patch_id;
	} rompatch_install;
	struct {
		__le32 patch_id;
	} rompatch_uninstall;
	struct {
		/* 0 = nothing executed
		 * otherwise = NVRAM segment return value
		 */
		__le32 result;
	} nvram_process;
	u8 payload[BMI_MAX_CMDBUF_SIZE];
} __packed;

struct bmi_target_info {
	u32 version;
	u32 type;
};

/* in msec */
#define BMI_COMMUNICATION_TIMEOUT_HZ (2 * HZ)

#define BMI_CE_NUM_TO_TARG 0
#define BMI_CE_NUM_TO_HOST 1

void ath10k_bmi_start(struct ath10k *ar);
int ath10k_bmi_done(struct ath10k *ar);
int ath10k_bmi_get_target_info(struct ath10k *ar,
			       struct bmi_target_info *target_info);
int ath10k_bmi_get_target_info_sdio(struct ath10k *ar,
				    struct bmi_target_info *target_info);
int ath10k_bmi_read_memory(struct ath10k *ar, u32 address,
			   void *buffer, u32 length);
int ath10k_bmi_write_memory(struct ath10k *ar, u32 address,
			    const void *buffer, u32 length);

#define ath10k_bmi_read32(ar, item, val)				\
	({								\
		int ret;						\
		u32 addr;						\
		__le32 tmp;						\
									\
		addr = host_interest_item_address(HI_ITEM(item));	\
		ret = ath10k_bmi_read_memory(ar, addr, (u8 *)&tmp, 4); \
		if (!ret)						\
			*val = __le32_to_cpu(tmp);			\
		ret;							\
	 })

#define ath10k_bmi_write32(ar, item, val)				\
	({								\
		int ret;						\
		u32 address;						\
		__le32 v = __cpu_to_le32(val);				\
									\
		address = host_interest_item_address(HI_ITEM(item));	\
		ret = ath10k_bmi_write_memory(ar, address,		\
					      (u8 *)&v, sizeof(v));	\
		ret;							\
	})

int ath10k_bmi_execute(struct ath10k *ar, u32 address, u32 param, u32 *result);
int ath10k_bmi_lz_stream_start(struct ath10k *ar, u32 address);
int ath10k_bmi_lz_data(struct ath10k *ar, const void *buffer, u32 length);
int ath10k_bmi_fast_download(struct ath10k *ar, u32 address,
			     const void *buffer, u32 length);
int ath10k_bmi_read_soc_reg(struct ath10k *ar, u32 address, u32 *reg_val);
int ath10k_bmi_write_soc_reg(struct ath10k *ar, u32 address, u32 reg_val);
#endif /* _BMI_H_ */
