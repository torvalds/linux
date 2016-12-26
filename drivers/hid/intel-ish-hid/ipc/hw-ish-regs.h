/*
 * ISH registers definitions
 *
 * Copyright (c) 2012-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#ifndef _ISHTP_ISH_REGS_H_
#define _ISHTP_ISH_REGS_H_


/*** IPC PCI Offsets and sizes ***/
/* ISH IPC Base Address */
#define IPC_REG_BASE		0x0000
/* Peripheral Interrupt Status Register */
#define IPC_REG_PISR_CHV_AB      (IPC_REG_BASE + 0x00)
/* Peripheral Interrupt Mask Register */
#define IPC_REG_PIMR_CHV_AB      (IPC_REG_BASE + 0x04)
/*BXT, CHV_K0*/
/*Peripheral Interrupt Status Register */
#define IPC_REG_PISR_BXT	 (IPC_REG_BASE + 0x0C)
/*Peripheral Interrupt Mask Register */
#define IPC_REG_PIMR_BXT	 (IPC_REG_BASE + 0x08)
/***********************************/
/* ISH Host Firmware status Register */
#define IPC_REG_ISH_HOST_FWSTS	(IPC_REG_BASE + 0x34)
/* Host Communication Register */
#define IPC_REG_HOST_COMM	(IPC_REG_BASE + 0x38)
/* Reset register */
#define IPC_REG_ISH_RST		(IPC_REG_BASE + 0x44)

/* Inbound doorbell register Host to ISH */
#define IPC_REG_HOST2ISH_DRBL	(IPC_REG_BASE + 0x48)
/* Outbound doorbell register ISH to Host */
#define IPC_REG_ISH2HOST_DRBL	(IPC_REG_BASE + 0x54)
/* ISH to HOST message registers */
#define IPC_REG_ISH2HOST_MSG	(IPC_REG_BASE + 0x60)
/* HOST to ISH message registers */
#define IPC_REG_HOST2ISH_MSG	(IPC_REG_BASE + 0xE0)
/* REMAP2 to enable DMA (D3 RCR) */
#define	IPC_REG_ISH_RMP2	(IPC_REG_BASE + 0x368)

#define	IPC_REG_MAX		(IPC_REG_BASE + 0x400)

/*** register bits - HISR ***/
/* bit corresponds HOST2ISH interrupt in PISR and PIMR registers */
#define IPC_INT_HOST2ISH_BIT            (1<<0)
/***********************************/
/*CHV_A0, CHV_B0*/
/* bit corresponds ISH2HOST interrupt in PISR and PIMR registers */
#define IPC_INT_ISH2HOST_BIT_CHV_AB	(1<<3)
/*BXT, CHV_K0*/
/* bit corresponds ISH2HOST interrupt in PISR and PIMR registers */
#define IPC_INT_ISH2HOST_BIT_BXT	(1<<0)
/***********************************/

/* bit corresponds ISH2HOST busy clear interrupt in PIMR register */
#define IPC_INT_ISH2HOST_CLR_MASK_BIT	(1<<11)

/* offset of ISH2HOST busy clear interrupt in IPC_BUSY_CLR register */
#define IPC_INT_ISH2HOST_CLR_OFFS	(0)

/* bit corresponds ISH2HOST busy clear interrupt in IPC_BUSY_CLR register */
#define IPC_INT_ISH2HOST_CLR_BIT	(1<<IPC_INT_ISH2HOST_CLR_OFFS)

/* bit corresponds busy bit in doorbell registers */
#define IPC_DRBL_BUSY_OFFS		(31)
#define IPC_DRBL_BUSY_BIT		(1<<IPC_DRBL_BUSY_OFFS)

#define	IPC_HOST_OWNS_MSG_OFFS		(30)

/*
 * A0: bit means that host owns MSGnn registers and is reading them.
 * ISH FW may not write to them
 */
#define	IPC_HOST_OWNS_MSG_BIT		(1<<IPC_HOST_OWNS_MSG_OFFS)

/*
 * Host status bits (HOSTCOMM)
 */
/* bit corresponds host ready bit in Host Status Register (HOST_COMM) */
#define IPC_HOSTCOMM_READY_OFFS		(7)
#define IPC_HOSTCOMM_READY_BIT		(1<<IPC_HOSTCOMM_READY_OFFS)

/***********************************/
/*CHV_A0, CHV_B0*/
#define	IPC_HOSTCOMM_INT_EN_OFFS_CHV_AB	(31)
#define	IPC_HOSTCOMM_INT_EN_BIT_CHV_AB		\
	(1<<IPC_HOSTCOMM_INT_EN_OFFS_CHV_AB)
/*BXT, CHV_K0*/
#define IPC_PIMR_INT_EN_OFFS_BXT	(0)
#define IPC_PIMR_INT_EN_BIT_BXT		(1<<IPC_PIMR_INT_EN_OFFS_BXT)

#define IPC_HOST2ISH_BUSYCLEAR_MASK_OFFS_BXT	(8)
#define IPC_HOST2ISH_BUSYCLEAR_MASK_BIT		\
	(1<<IPC_HOST2ISH_BUSYCLEAR_MASK_OFFS_BXT)
/***********************************/
/*
 * both Host and ISH have ILUP at bit 0
 * bit corresponds host ready bit in both status registers
 */
#define IPC_ILUP_OFFS			(0)
#define IPC_ILUP_BIT			(1<<IPC_ILUP_OFFS)

/*
 * FW status bits (relevant)
 */
#define	IPC_FWSTS_ILUP		0x1
#define	IPC_FWSTS_ISHTP_UP	(1<<1)
#define	IPC_FWSTS_DMA0		(1<<16)
#define	IPC_FWSTS_DMA1		(1<<17)
#define	IPC_FWSTS_DMA2		(1<<18)
#define	IPC_FWSTS_DMA3		(1<<19)

#define	IPC_ISH_IN_DMA		\
	(IPC_FWSTS_DMA0 | IPC_FWSTS_DMA1 | IPC_FWSTS_DMA2 | IPC_FWSTS_DMA3)

/* bit corresponds host ready bit in ISH FW Status Register */
#define IPC_ISH_ISHTP_READY_OFFS		(1)
#define IPC_ISH_ISHTP_READY_BIT		(1<<IPC_ISH_ISHTP_READY_OFFS)

#define	IPC_RMP2_DMA_ENABLED	0x1	/* Value to enable DMA, per D3 RCR */

#define IPC_MSG_MAX_SIZE	0x80


#define IPC_HEADER_LENGTH_MASK		0x03FF
#define IPC_HEADER_PROTOCOL_MASK	0x0F
#define IPC_HEADER_MNG_CMD_MASK		0x0F

#define IPC_HEADER_LENGTH_OFFSET	0
#define IPC_HEADER_PROTOCOL_OFFSET	10
#define IPC_HEADER_MNG_CMD_OFFSET	16

#define IPC_HEADER_GET_LENGTH(drbl_reg)		\
	(((drbl_reg) >> IPC_HEADER_LENGTH_OFFSET)&IPC_HEADER_LENGTH_MASK)
#define IPC_HEADER_GET_PROTOCOL(drbl_reg)	\
	(((drbl_reg) >> IPC_HEADER_PROTOCOL_OFFSET)&IPC_HEADER_PROTOCOL_MASK)
#define IPC_HEADER_GET_MNG_CMD(drbl_reg)	\
	(((drbl_reg) >> IPC_HEADER_MNG_CMD_OFFSET)&IPC_HEADER_MNG_CMD_MASK)

#define IPC_IS_BUSY(drbl_reg)			\
	(((drbl_reg)&IPC_DRBL_BUSY_BIT) == ((uint32_t)IPC_DRBL_BUSY_BIT))

/***********************************/
/*CHV_A0, CHV_B0*/
#define IPC_INT_FROM_ISH_TO_HOST_CHV_AB(drbl_reg) \
	(((drbl_reg)&IPC_INT_ISH2HOST_BIT_CHV_AB) == \
	((u32)IPC_INT_ISH2HOST_BIT_CHV_AB))
/*BXT, CHV_K0*/
#define IPC_INT_FROM_ISH_TO_HOST_BXT(drbl_reg) \
	(((drbl_reg)&IPC_INT_ISH2HOST_BIT_BXT) == \
	((u32)IPC_INT_ISH2HOST_BIT_BXT))
/***********************************/

#define IPC_BUILD_HEADER(length, protocol, busy)		\
	(((busy)<<IPC_DRBL_BUSY_OFFS) |				\
	((protocol) << IPC_HEADER_PROTOCOL_OFFSET) |		\
	((length)<<IPC_HEADER_LENGTH_OFFSET))

#define IPC_BUILD_MNG_MSG(cmd, length)				\
	(((1)<<IPC_DRBL_BUSY_OFFS)|				\
	((IPC_PROTOCOL_MNG)<<IPC_HEADER_PROTOCOL_OFFSET)|	\
	((cmd)<<IPC_HEADER_MNG_CMD_OFFSET)|			\
	 ((length)<<IPC_HEADER_LENGTH_OFFSET))


#define IPC_SET_HOST_READY(host_status)		\
				((host_status) |= (IPC_HOSTCOMM_READY_BIT))

#define IPC_SET_HOST_ILUP(host_status)		\
				((host_status) |= (IPC_ILUP_BIT))

#define IPC_CLEAR_HOST_READY(host_status)	\
				((host_status) ^= (IPC_HOSTCOMM_READY_BIT))

#define IPC_CLEAR_HOST_ILUP(host_status)	\
				((host_status) ^= (IPC_ILUP_BIT))

/* todo - temp until PIMR HW ready */
#define IPC_HOST_BUSY_READING_OFFS	6

/* bit corresponds host ready bit in Host Status Register (HOST_COMM) */
#define IPC_HOST_BUSY_READING_BIT	(1<<IPC_HOST_BUSY_READING_OFFS)

#define IPC_SET_HOST_BUSY_READING(host_status)	\
				((host_status) |= (IPC_HOST_BUSY_READING_BIT))

#define IPC_CLEAR_HOST_BUSY_READING(host_status)\
				((host_status) ^= (IPC_HOST_BUSY_READING_BIT))


#define IPC_IS_ISH_ISHTP_READY(ish_status)	\
		(((ish_status) & IPC_ISH_ISHTP_READY_BIT) ==	\
			((uint32_t)IPC_ISH_ISHTP_READY_BIT))

#define IPC_IS_ISH_ILUP(ish_status)		\
		(((ish_status) & IPC_ILUP_BIT) == ((uint32_t)IPC_ILUP_BIT))


#define IPC_PROTOCOL_ISHTP		1
#define IPC_PROTOCOL_MNG		3

#define MNG_RX_CMPL_ENABLE		0
#define MNG_RX_CMPL_DISABLE		1
#define MNG_RX_CMPL_INDICATION		2
#define MNG_RESET_NOTIFY		3
#define MNG_RESET_NOTIFY_ACK		4
#define MNG_SYNC_FW_CLOCK		5
#define MNG_ILLEGAL_CMD			0xFF

#endif /* _ISHTP_ISH_REGS_H_ */
