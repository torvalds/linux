/*
 * Low-level API for mac80211 ST-Ericsson CW1200 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * Based on:
 * ST-Ericsson UMAC CW1200 driver which is
 * Copyright (c) 2010, ST-Ericsson
 * Author: Ajitpal Singh <ajitpal.singh@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CW1200_HWIO_H_INCLUDED
#define CW1200_HWIO_H_INCLUDED

/* extern */ struct cw1200_common;

#define CW1200_CUT_11_ID_STR		(0x302E3830)
#define CW1200_CUT_22_ID_STR1		(0x302e3132)
#define CW1200_CUT_22_ID_STR2		(0x32302e30)
#define CW1200_CUT_22_ID_STR3		(0x3335)
#define CW1200_CUT_ID_ADDR		(0xFFF17F90)
#define CW1200_CUT2_ID_ADDR		(0xFFF1FF90)

/* Download control area */
/* boot loader start address in SRAM */
#define DOWNLOAD_BOOT_LOADER_OFFSET	(0x00000000)
/* 32K, 0x4000 to 0xDFFF */
#define DOWNLOAD_FIFO_OFFSET		(0x00004000)
/* 32K */
#define DOWNLOAD_FIFO_SIZE		(0x00008000)
/* 128 bytes, 0xFF80 to 0xFFFF */
#define DOWNLOAD_CTRL_OFFSET		(0x0000FF80)
#define DOWNLOAD_CTRL_DATA_DWORDS	(32-6)

struct download_cntl_t {
	/* size of whole firmware file (including Cheksum), host init */
	u32 image_size;
	/* downloading flags */
	u32 flags;
	/* No. of bytes put into the download, init & updated by host */
	u32 put;
	/* last traced program counter, last ARM reg_pc */
	u32 trace_pc;
	/* No. of bytes read from the download, host init, device updates */
	u32 get;
	/* r0, boot losader status, host init to pending, device updates */
	u32 status;
	/* Extra debug info, r1 to r14 if status=r0=DOWNLOAD_EXCEPTION */
	u32 debug_data[DOWNLOAD_CTRL_DATA_DWORDS];
};

#define	DOWNLOAD_IMAGE_SIZE_REG		\
	(DOWNLOAD_CTRL_OFFSET + offsetof(struct download_cntl_t, image_size))
#define	DOWNLOAD_FLAGS_REG		\
	(DOWNLOAD_CTRL_OFFSET + offsetof(struct download_cntl_t, flags))
#define DOWNLOAD_PUT_REG		\
	(DOWNLOAD_CTRL_OFFSET + offsetof(struct download_cntl_t, put))
#define DOWNLOAD_TRACE_PC_REG		\
	(DOWNLOAD_CTRL_OFFSET + offsetof(struct download_cntl_t, trace_pc))
#define	DOWNLOAD_GET_REG		\
	(DOWNLOAD_CTRL_OFFSET + offsetof(struct download_cntl_t, get))
#define	DOWNLOAD_STATUS_REG		\
	(DOWNLOAD_CTRL_OFFSET + offsetof(struct download_cntl_t, status))
#define DOWNLOAD_DEBUG_DATA_REG		\
	(DOWNLOAD_CTRL_OFFSET + offsetof(struct download_cntl_t, debug_data))
#define DOWNLOAD_DEBUG_DATA_LEN		(108)

#define DOWNLOAD_BLOCK_SIZE		(1024)

/* For boot loader detection */
#define DOWNLOAD_ARE_YOU_HERE		(0x87654321)
#define DOWNLOAD_I_AM_HERE		(0x12345678)

/* Download error code */
#define DOWNLOAD_PENDING		(0xFFFFFFFF)
#define DOWNLOAD_SUCCESS		(0)
#define DOWNLOAD_EXCEPTION		(1)
#define DOWNLOAD_ERR_MEM_1		(2)
#define DOWNLOAD_ERR_MEM_2		(3)
#define DOWNLOAD_ERR_SOFTWARE		(4)
#define DOWNLOAD_ERR_FILE_SIZE		(5)
#define DOWNLOAD_ERR_CHECKSUM		(6)
#define DOWNLOAD_ERR_OVERFLOW		(7)
#define DOWNLOAD_ERR_IMAGE		(8)
#define DOWNLOAD_ERR_HOST		(9)
#define DOWNLOAD_ERR_ABORT		(10)


#define SYS_BASE_ADDR_SILICON		(0)
#define PAC_BASE_ADDRESS_SILICON	(SYS_BASE_ADDR_SILICON + 0x09000000)
#define PAC_SHARED_MEMORY_SILICON	(PAC_BASE_ADDRESS_SILICON)

#define CW1200_APB(addr)		(PAC_SHARED_MEMORY_SILICON + (addr))

/* ***************************************************************
*Device register definitions
*************************************************************** */
/* WBF - SPI Register Addresses */
#define ST90TDS_ADDR_ID_BASE		(0x0000)
/* 16/32 bits */
#define ST90TDS_CONFIG_REG_ID		(0x0000)
/* 16/32 bits */
#define ST90TDS_CONTROL_REG_ID		(0x0001)
/* 16 bits, Q mode W/R */
#define ST90TDS_IN_OUT_QUEUE_REG_ID	(0x0002)
/* 32 bits, AHB bus R/W */
#define ST90TDS_AHB_DPORT_REG_ID	(0x0003)
/* 16/32 bits */
#define ST90TDS_SRAM_BASE_ADDR_REG_ID   (0x0004)
/* 32 bits, APB bus R/W */
#define ST90TDS_SRAM_DPORT_REG_ID	(0x0005)
/* 32 bits, t_settle/general */
#define ST90TDS_TSET_GEN_R_W_REG_ID	(0x0006)
/* 16 bits, Q mode read, no length */
#define ST90TDS_FRAME_OUT_REG_ID	(0x0007)
#define ST90TDS_ADDR_ID_MAX		(ST90TDS_FRAME_OUT_REG_ID)

/* WBF - Control register bit set */
/* next o/p length, bit 11 to 0 */
#define ST90TDS_CONT_NEXT_LEN_MASK	(0x0FFF)
#define ST90TDS_CONT_WUP_BIT		(BIT(12))
#define ST90TDS_CONT_RDY_BIT		(BIT(13))
#define ST90TDS_CONT_IRQ_ENABLE		(BIT(14))
#define ST90TDS_CONT_RDY_ENABLE		(BIT(15))
#define ST90TDS_CONT_IRQ_RDY_ENABLE	(BIT(14)|BIT(15))

/* SPI Config register bit set */
#define ST90TDS_CONFIG_FRAME_BIT	(BIT(2))
#define ST90TDS_CONFIG_WORD_MODE_BITS	(BIT(3)|BIT(4))
#define ST90TDS_CONFIG_WORD_MODE_1	(BIT(3))
#define ST90TDS_CONFIG_WORD_MODE_2	(BIT(4))
#define ST90TDS_CONFIG_ERROR_0_BIT	(BIT(5))
#define ST90TDS_CONFIG_ERROR_1_BIT	(BIT(6))
#define ST90TDS_CONFIG_ERROR_2_BIT	(BIT(7))
/* TBD: Sure??? */
#define ST90TDS_CONFIG_CSN_FRAME_BIT	(BIT(7))
#define ST90TDS_CONFIG_ERROR_3_BIT	(BIT(8))
#define ST90TDS_CONFIG_ERROR_4_BIT	(BIT(9))
/* QueueM */
#define ST90TDS_CONFIG_ACCESS_MODE_BIT	(BIT(10))
/* AHB bus */
#define ST90TDS_CONFIG_AHB_PRFETCH_BIT	(BIT(11))
#define ST90TDS_CONFIG_CPU_CLK_DIS_BIT	(BIT(12))
/* APB bus */
#define ST90TDS_CONFIG_PRFETCH_BIT	(BIT(13))
/* cpu reset */
#define ST90TDS_CONFIG_CPU_RESET_BIT	(BIT(14))
#define ST90TDS_CONFIG_CLEAR_INT_BIT	(BIT(15))

/* For CW1200 the IRQ Enable and Ready Bits are in CONFIG register */
#define ST90TDS_CONF_IRQ_ENABLE		(BIT(16))
#define ST90TDS_CONF_RDY_ENABLE		(BIT(17))
#define ST90TDS_CONF_IRQ_RDY_ENABLE	(BIT(16)|BIT(17))

int cw1200_data_read(struct cw1200_common *priv,
		     void *buf, size_t buf_len);
int cw1200_data_write(struct cw1200_common *priv,
		      const void *buf, size_t buf_len);

int cw1200_reg_read(struct cw1200_common *priv, u16 addr,
		    void *buf, size_t buf_len);
int cw1200_reg_write(struct cw1200_common *priv, u16 addr,
		     const void *buf, size_t buf_len);

static inline int cw1200_reg_read_16(struct cw1200_common *priv,
				     u16 addr, u16 *val)
{
	u32 tmp;
	int i;
	i = cw1200_reg_read(priv, addr, &tmp, sizeof(tmp));
	tmp = le32_to_cpu(tmp);
	*val = tmp & 0xffff;
	return i;
}

static inline int cw1200_reg_write_16(struct cw1200_common *priv,
				      u16 addr, u16 val)
{
	u32 tmp = val;
	tmp = cpu_to_le32(tmp);
	return cw1200_reg_write(priv, addr, &tmp, sizeof(tmp));
}

static inline int cw1200_reg_read_32(struct cw1200_common *priv,
				     u16 addr, u32 *val)
{
	int i = cw1200_reg_read(priv, addr, val, sizeof(*val));
	*val = le32_to_cpu(*val);
	return i;
}

static inline int cw1200_reg_write_32(struct cw1200_common *priv,
				      u16 addr, u32 val)
{
	val = cpu_to_le32(val);
	return cw1200_reg_write(priv, addr, &val, sizeof(val));
}

int cw1200_indirect_read(struct cw1200_common *priv, u32 addr, void *buf,
			 size_t buf_len, u32 prefetch, u16 port_addr);
int cw1200_apb_write(struct cw1200_common *priv, u32 addr, const void *buf,
		     size_t buf_len);

static inline int cw1200_apb_read(struct cw1200_common *priv, u32 addr,
				  void *buf, size_t buf_len)
{
	return cw1200_indirect_read(priv, addr, buf, buf_len,
				    ST90TDS_CONFIG_PRFETCH_BIT,
				    ST90TDS_SRAM_DPORT_REG_ID);
}

static inline int cw1200_ahb_read(struct cw1200_common *priv, u32 addr,
				  void *buf, size_t buf_len)
{
	return cw1200_indirect_read(priv, addr, buf, buf_len,
				    ST90TDS_CONFIG_AHB_PRFETCH_BIT,
				    ST90TDS_AHB_DPORT_REG_ID);
}

static inline int cw1200_apb_read_32(struct cw1200_common *priv,
				     u32 addr, u32 *val)
{
	int i = cw1200_apb_read(priv, addr, val, sizeof(*val));
	*val = le32_to_cpu(*val);
	return i;
}

static inline int cw1200_apb_write_32(struct cw1200_common *priv,
				      u32 addr, u32 val)
{
	val = cpu_to_le32(val);
	return cw1200_apb_write(priv, addr, &val, sizeof(val));
}
static inline int cw1200_ahb_read_32(struct cw1200_common *priv,
				     u32 addr, u32 *val)
{
	int i = cw1200_ahb_read(priv, addr, val, sizeof(*val));
	*val = le32_to_cpu(*val);
	return i;
}

#endif /* CW1200_HWIO_H_INCLUDED */
