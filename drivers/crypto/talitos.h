/*
 * Freescale SEC (talitos) device register and descriptor header defines
 *
 * Copyright (c) 2006-2011 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#define TALITOS_TIMEOUT 100000
#define TALITOS_MAX_DATA_LEN 65535

#define DESC_TYPE(desc_hdr) ((be32_to_cpu(desc_hdr) >> 3) & 0x1f)
#define PRIMARY_EU(desc_hdr) ((be32_to_cpu(desc_hdr) >> 28) & 0xf)
#define SECONDARY_EU(desc_hdr) ((be32_to_cpu(desc_hdr) >> 16) & 0xf)

/* descriptor pointer entry */
struct talitos_ptr {
	__be16 len;     /* length */
	u8 j_extent;    /* jump to sg link table and/or extent */
	u8 eptr;        /* extended address */
	__be32 ptr;     /* address */
};

static const struct talitos_ptr zero_entry = {
	.len = 0,
	.j_extent = 0,
	.eptr = 0,
	.ptr = 0
};

/* descriptor */
struct talitos_desc {
	__be32 hdr;                     /* header high bits */
	__be32 hdr_lo;                  /* header low bits */
	struct talitos_ptr ptr[7];      /* ptr/len pair array */
};

/**
 * talitos_request - descriptor submission request
 * @desc: descriptor pointer (kernel virtual)
 * @dma_desc: descriptor's physical bus address
 * @callback: whom to call when descriptor processing is done
 * @context: caller context (optional)
 */
struct talitos_request {
	struct talitos_desc *desc;
	dma_addr_t dma_desc;
	void (*callback) (struct device *dev, struct talitos_desc *desc,
			  void *context, int error);
	void *context;
};

/* per-channel fifo management */
struct talitos_channel {
	void __iomem *reg;

	/* request fifo */
	struct talitos_request *fifo;

	/* number of requests pending in channel h/w fifo */
	atomic_t submit_count ____cacheline_aligned;

	/* request submission (head) lock */
	spinlock_t head_lock ____cacheline_aligned;
	/* index to next free descriptor request */
	int head;

	/* request release (tail) lock */
	spinlock_t tail_lock ____cacheline_aligned;
	/* index to next in-progress/done descriptor request */
	int tail;
};

struct talitos_private {
	struct device *dev;
	struct platform_device *ofdev;
	void __iomem *reg;
	int irq[2];

	/* SEC global registers lock  */
	spinlock_t reg_lock ____cacheline_aligned;

	/* SEC version geometry (from device tree node) */
	unsigned int num_channels;
	unsigned int chfifo_len;
	unsigned int exec_units;
	unsigned int desc_types;

	/* SEC Compatibility info */
	unsigned long features;

	/*
	 * length of the request fifo
	 * fifo_len is chfifo_len rounded up to next power of 2
	 * so we can use bitwise ops to wrap
	 */
	unsigned int fifo_len;

	struct talitos_channel *chan;

	/* next channel to be assigned next incoming descriptor */
	atomic_t last_chan ____cacheline_aligned;

	/* request callback tasklet */
	struct tasklet_struct done_task[2];

	/* list of registered algorithms */
	struct list_head alg_list;

	/* hwrng device */
	struct hwrng rng;
};

/* .features flag */
#define TALITOS_FTR_SRC_LINK_TBL_LEN_INCLUDES_EXTENT 0x00000001
#define TALITOS_FTR_HW_AUTH_CHECK 0x00000002
#define TALITOS_FTR_SHA224_HWINIT 0x00000004
#define TALITOS_FTR_HMAC_OK 0x00000008

/*
 * TALITOS_xxx_LO addresses point to the low data bits (32-63) of the register
 */

/* global register offset addresses */
#define TALITOS_MCR			0x1030  /* master control register */
#define   TALITOS_MCR_RCA0		(1 << 15) /* remap channel 0 */
#define   TALITOS_MCR_RCA1		(1 << 14) /* remap channel 1 */
#define   TALITOS_MCR_RCA2		(1 << 13) /* remap channel 2 */
#define   TALITOS_MCR_RCA3		(1 << 12) /* remap channel 3 */
#define   TALITOS_MCR_SWR		0x1     /* s/w reset */
#define TALITOS_MCR_LO			0x1034
#define TALITOS_IMR			0x1008  /* interrupt mask register */
#define   TALITOS_IMR_INIT		0x100ff /* enable channel IRQs */
#define   TALITOS_IMR_DONE		0x00055 /* done IRQs */
#define TALITOS_IMR_LO			0x100C
#define   TALITOS_IMR_LO_INIT		0x20000 /* allow RNGU error IRQs */
#define TALITOS_ISR			0x1010  /* interrupt status register */
#define   TALITOS_ISR_4CHERR		0xaa    /* 4 channel errors mask */
#define   TALITOS_ISR_4CHDONE		0x55    /* 4 channel done mask */
#define   TALITOS_ISR_CH_0_2_ERR	0x22    /* channels 0, 2 errors mask */
#define   TALITOS_ISR_CH_0_2_DONE	0x11    /* channels 0, 2 done mask */
#define   TALITOS_ISR_CH_1_3_ERR	0x88    /* channels 1, 3 errors mask */
#define   TALITOS_ISR_CH_1_3_DONE	0x44    /* channels 1, 3 done mask */
#define TALITOS_ISR_LO			0x1014
#define TALITOS_ICR			0x1018  /* interrupt clear register */
#define TALITOS_ICR_LO			0x101C

/* channel register address stride */
#define TALITOS_CH_BASE_OFFSET		0x1000	/* default channel map base */
#define TALITOS_CH_STRIDE		0x100

/* channel configuration register  */
#define TALITOS_CCCR			0x8
#define   TALITOS_CCCR_CONT		0x2    /* channel continue */
#define   TALITOS_CCCR_RESET		0x1    /* channel reset */
#define TALITOS_CCCR_LO			0xc
#define   TALITOS_CCCR_LO_IWSE		0x80   /* chan. ICCR writeback enab. */
#define   TALITOS_CCCR_LO_EAE		0x20   /* extended address enable */
#define   TALITOS_CCCR_LO_CDWE		0x10   /* chan. done writeback enab. */
#define   TALITOS_CCCR_LO_NT		0x4    /* notification type */
#define   TALITOS_CCCR_LO_CDIE		0x2    /* channel done IRQ enable */

/* CCPSR: channel pointer status register */
#define TALITOS_CCPSR			0x10
#define TALITOS_CCPSR_LO		0x14
#define   TALITOS_CCPSR_LO_DOF		0x8000 /* double FF write oflow error */
#define   TALITOS_CCPSR_LO_SOF		0x4000 /* single FF write oflow error */
#define   TALITOS_CCPSR_LO_MDTE		0x2000 /* master data transfer error */
#define   TALITOS_CCPSR_LO_SGDLZ	0x1000 /* s/g data len zero error */
#define   TALITOS_CCPSR_LO_FPZ		0x0800 /* fetch ptr zero error */
#define   TALITOS_CCPSR_LO_IDH		0x0400 /* illegal desc hdr error */
#define   TALITOS_CCPSR_LO_IEU		0x0200 /* invalid EU error */
#define   TALITOS_CCPSR_LO_EU		0x0100 /* EU error detected */
#define   TALITOS_CCPSR_LO_GB		0x0080 /* gather boundary error */
#define   TALITOS_CCPSR_LO_GRL		0x0040 /* gather return/length error */
#define   TALITOS_CCPSR_LO_SB		0x0020 /* scatter boundary error */
#define   TALITOS_CCPSR_LO_SRL		0x0010 /* scatter return/length error */

/* channel fetch fifo register */
#define TALITOS_FF			0x48
#define TALITOS_FF_LO			0x4c

/* current descriptor pointer register */
#define TALITOS_CDPR			0x40
#define TALITOS_CDPR_LO			0x44

/* descriptor buffer register */
#define TALITOS_DESCBUF			0x80
#define TALITOS_DESCBUF_LO		0x84

/* gather link table */
#define TALITOS_GATHER			0xc0
#define TALITOS_GATHER_LO		0xc4

/* scatter link table */
#define TALITOS_SCATTER			0xe0
#define TALITOS_SCATTER_LO		0xe4

/* execution unit interrupt status registers */
#define TALITOS_DEUISR			0x2030 /* DES unit */
#define TALITOS_DEUISR_LO		0x2034
#define TALITOS_AESUISR			0x4030 /* AES unit */
#define TALITOS_AESUISR_LO		0x4034
#define TALITOS_MDEUISR			0x6030 /* message digest unit */
#define TALITOS_MDEUISR_LO		0x6034
#define TALITOS_MDEUICR			0x6038 /* interrupt control */
#define TALITOS_MDEUICR_LO		0x603c
#define   TALITOS_MDEUICR_LO_ICE	0x4000 /* integrity check IRQ enable */
#define TALITOS_AFEUISR			0x8030 /* arc4 unit */
#define TALITOS_AFEUISR_LO		0x8034
#define TALITOS_RNGUISR			0xa030 /* random number unit */
#define TALITOS_RNGUISR_LO		0xa034
#define TALITOS_RNGUSR			0xa028 /* rng status */
#define TALITOS_RNGUSR_LO		0xa02c
#define   TALITOS_RNGUSR_LO_RD		0x1	/* reset done */
#define   TALITOS_RNGUSR_LO_OFL		0xff0000/* output FIFO length */
#define TALITOS_RNGUDSR			0xa010	/* data size */
#define TALITOS_RNGUDSR_LO		0xa014
#define TALITOS_RNGU_FIFO		0xa800	/* output FIFO */
#define TALITOS_RNGU_FIFO_LO		0xa804	/* output FIFO */
#define TALITOS_RNGURCR			0xa018	/* reset control */
#define TALITOS_RNGURCR_LO		0xa01c
#define   TALITOS_RNGURCR_LO_SR		0x1	/* software reset */
#define TALITOS_PKEUISR			0xc030 /* public key unit */
#define TALITOS_PKEUISR_LO		0xc034
#define TALITOS_KEUISR			0xe030 /* kasumi unit */
#define TALITOS_KEUISR_LO		0xe034
#define TALITOS_CRCUISR			0xf030 /* cyclic redundancy check unit*/
#define TALITOS_CRCUISR_LO		0xf034

#define TALITOS_MDEU_CONTEXT_SIZE_MD5_SHA1_SHA256	0x28
#define TALITOS_MDEU_CONTEXT_SIZE_SHA384_SHA512		0x48

/*
 * talitos descriptor header (hdr) bits
 */

/* written back when done */
#define DESC_HDR_DONE			cpu_to_be32(0xff000000)
#define DESC_HDR_LO_ICCR1_MASK		cpu_to_be32(0x00180000)
#define DESC_HDR_LO_ICCR1_PASS		cpu_to_be32(0x00080000)
#define DESC_HDR_LO_ICCR1_FAIL		cpu_to_be32(0x00100000)

/* primary execution unit select */
#define	DESC_HDR_SEL0_MASK		cpu_to_be32(0xf0000000)
#define	DESC_HDR_SEL0_AFEU		cpu_to_be32(0x10000000)
#define	DESC_HDR_SEL0_DEU		cpu_to_be32(0x20000000)
#define	DESC_HDR_SEL0_MDEUA		cpu_to_be32(0x30000000)
#define	DESC_HDR_SEL0_MDEUB		cpu_to_be32(0xb0000000)
#define	DESC_HDR_SEL0_RNG		cpu_to_be32(0x40000000)
#define	DESC_HDR_SEL0_PKEU		cpu_to_be32(0x50000000)
#define	DESC_HDR_SEL0_AESU		cpu_to_be32(0x60000000)
#define	DESC_HDR_SEL0_KEU		cpu_to_be32(0x70000000)
#define	DESC_HDR_SEL0_CRCU		cpu_to_be32(0x80000000)

/* primary execution unit mode (MODE0) and derivatives */
#define	DESC_HDR_MODE0_ENCRYPT		cpu_to_be32(0x00100000)
#define	DESC_HDR_MODE0_AESU_CBC		cpu_to_be32(0x00200000)
#define	DESC_HDR_MODE0_DEU_CBC		cpu_to_be32(0x00400000)
#define	DESC_HDR_MODE0_DEU_3DES		cpu_to_be32(0x00200000)
#define	DESC_HDR_MODE0_MDEU_CONT	cpu_to_be32(0x08000000)
#define	DESC_HDR_MODE0_MDEU_INIT	cpu_to_be32(0x01000000)
#define	DESC_HDR_MODE0_MDEU_HMAC	cpu_to_be32(0x00800000)
#define	DESC_HDR_MODE0_MDEU_PAD		cpu_to_be32(0x00400000)
#define	DESC_HDR_MODE0_MDEU_SHA224	cpu_to_be32(0x00300000)
#define	DESC_HDR_MODE0_MDEU_MD5		cpu_to_be32(0x00200000)
#define	DESC_HDR_MODE0_MDEU_SHA256	cpu_to_be32(0x00100000)
#define	DESC_HDR_MODE0_MDEU_SHA1	cpu_to_be32(0x00000000)
#define	DESC_HDR_MODE0_MDEUB_SHA384	cpu_to_be32(0x00000000)
#define	DESC_HDR_MODE0_MDEUB_SHA512	cpu_to_be32(0x00200000)
#define	DESC_HDR_MODE0_MDEU_MD5_HMAC	(DESC_HDR_MODE0_MDEU_MD5 | \
					 DESC_HDR_MODE0_MDEU_HMAC)
#define	DESC_HDR_MODE0_MDEU_SHA256_HMAC	(DESC_HDR_MODE0_MDEU_SHA256 | \
					 DESC_HDR_MODE0_MDEU_HMAC)
#define	DESC_HDR_MODE0_MDEU_SHA1_HMAC	(DESC_HDR_MODE0_MDEU_SHA1 | \
					 DESC_HDR_MODE0_MDEU_HMAC)

/* secondary execution unit select (SEL1) */
#define	DESC_HDR_SEL1_MASK		cpu_to_be32(0x000f0000)
#define	DESC_HDR_SEL1_MDEUA		cpu_to_be32(0x00030000)
#define	DESC_HDR_SEL1_MDEUB		cpu_to_be32(0x000b0000)
#define	DESC_HDR_SEL1_CRCU		cpu_to_be32(0x00080000)

/* secondary execution unit mode (MODE1) and derivatives */
#define	DESC_HDR_MODE1_MDEU_CICV	cpu_to_be32(0x00004000)
#define	DESC_HDR_MODE1_MDEU_INIT	cpu_to_be32(0x00001000)
#define	DESC_HDR_MODE1_MDEU_HMAC	cpu_to_be32(0x00000800)
#define	DESC_HDR_MODE1_MDEU_PAD		cpu_to_be32(0x00000400)
#define	DESC_HDR_MODE1_MDEU_SHA224	cpu_to_be32(0x00000300)
#define	DESC_HDR_MODE1_MDEU_MD5		cpu_to_be32(0x00000200)
#define	DESC_HDR_MODE1_MDEU_SHA256	cpu_to_be32(0x00000100)
#define	DESC_HDR_MODE1_MDEU_SHA1	cpu_to_be32(0x00000000)
#define	DESC_HDR_MODE1_MDEUB_SHA384	cpu_to_be32(0x00000000)
#define	DESC_HDR_MODE1_MDEUB_SHA512	cpu_to_be32(0x00000200)
#define	DESC_HDR_MODE1_MDEU_MD5_HMAC	(DESC_HDR_MODE1_MDEU_MD5 | \
					 DESC_HDR_MODE1_MDEU_HMAC)
#define	DESC_HDR_MODE1_MDEU_SHA256_HMAC	(DESC_HDR_MODE1_MDEU_SHA256 | \
					 DESC_HDR_MODE1_MDEU_HMAC)
#define	DESC_HDR_MODE1_MDEU_SHA1_HMAC	(DESC_HDR_MODE1_MDEU_SHA1 | \
					 DESC_HDR_MODE1_MDEU_HMAC)

/* direction of overall data flow (DIR) */
#define	DESC_HDR_DIR_INBOUND		cpu_to_be32(0x00000002)

/* request done notification (DN) */
#define	DESC_HDR_DONE_NOTIFY		cpu_to_be32(0x00000001)

/* descriptor types */
#define DESC_HDR_TYPE_AESU_CTR_NONSNOOP		cpu_to_be32(0 << 3)
#define DESC_HDR_TYPE_IPSEC_ESP			cpu_to_be32(1 << 3)
#define DESC_HDR_TYPE_COMMON_NONSNOOP_NO_AFEU	cpu_to_be32(2 << 3)
#define DESC_HDR_TYPE_HMAC_SNOOP_NO_AFEU	cpu_to_be32(4 << 3)

/* link table extent field bits */
#define DESC_PTR_LNKTBL_JUMP			0x80
#define DESC_PTR_LNKTBL_RETURN			0x02
#define DESC_PTR_LNKTBL_NEXT			0x01
