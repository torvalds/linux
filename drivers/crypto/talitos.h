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
#define TALITOS1_MAX_DATA_LEN 32768
#define TALITOS2_MAX_DATA_LEN 65535

#define DESC_TYPE(desc_hdr) ((be32_to_cpu(desc_hdr) >> 3) & 0x1f)
#define PRIMARY_EU(desc_hdr) ((be32_to_cpu(desc_hdr) >> 28) & 0xf)
#define SECONDARY_EU(desc_hdr) ((be32_to_cpu(desc_hdr) >> 16) & 0xf)

/* descriptor pointer entry */
struct talitos_ptr {
	union {
		struct {		/* SEC2 format */
			__be16 len;     /* length */
			u8 j_extent;    /* jump to sg link table and/or extent*/
			u8 eptr;        /* extended address */
		};
		struct {			/* SEC1 format */
			__be16 res;
			__be16 len1;	/* length */
		};
	};
	__be32 ptr;     /* address */
};

static const struct talitos_ptr zero_entry;

/* descriptor */
struct talitos_desc {
	__be32 hdr;                     /* header high bits */
	union {
		__be32 hdr_lo;		/* header low bits */
		__be32 hdr1;		/* header for SEC1 */
	};
	struct talitos_ptr ptr[7];      /* ptr/len pair array */
	__be32 next_desc;		/* next descriptor (SEC1) */
};

#define TALITOS_DESC_SIZE	(sizeof(struct talitos_desc) - sizeof(__be32))

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
	void __iomem *reg_deu;
	void __iomem *reg_aesu;
	void __iomem *reg_mdeu;
	void __iomem *reg_afeu;
	void __iomem *reg_rngu;
	void __iomem *reg_pkeu;
	void __iomem *reg_keu;
	void __iomem *reg_crcu;
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

extern int talitos_submit(struct device *dev, int ch, struct talitos_desc *desc,
			  void (*callback)(struct device *dev,
					   struct talitos_desc *desc,
					   void *context, int error),
			  void *context);

/* .features flag */
#define TALITOS_FTR_SRC_LINK_TBL_LEN_INCLUDES_EXTENT 0x00000001
#define TALITOS_FTR_HW_AUTH_CHECK 0x00000002
#define TALITOS_FTR_SHA224_HWINIT 0x00000004
#define TALITOS_FTR_HMAC_OK 0x00000008
#define TALITOS_FTR_SEC1 0x00000010

/*
 * If both CONFIG_CRYPTO_DEV_TALITOS1 and CONFIG_CRYPTO_DEV_TALITOS2 are
 * defined, we check the features which are set according to the device tree.
 * Otherwise, we answer true or false directly
 */
static inline bool has_ftr_sec1(struct talitos_private *priv)
{
#if defined(CONFIG_CRYPTO_DEV_TALITOS1) && defined(CONFIG_CRYPTO_DEV_TALITOS2)
	return priv->features & TALITOS_FTR_SEC1 ? true : false;
#elif defined(CONFIG_CRYPTO_DEV_TALITOS1)
	return true;
#else
	return false;
#endif
}

/*
 * TALITOS_xxx_LO addresses point to the low data bits (32-63) of the register
 */

#define ISR1_FORMAT(x)			(((x) << 28) | ((x) << 16))
#define ISR2_FORMAT(x)			(((x) << 4) | (x))

/* global register offset addresses */
#define TALITOS_MCR			0x1030  /* master control register */
#define   TALITOS_MCR_RCA0		(1 << 15) /* remap channel 0 */
#define   TALITOS_MCR_RCA1		(1 << 14) /* remap channel 1 */
#define   TALITOS_MCR_RCA2		(1 << 13) /* remap channel 2 */
#define   TALITOS_MCR_RCA3		(1 << 12) /* remap channel 3 */
#define   TALITOS1_MCR_SWR		0x1000000     /* s/w reset */
#define   TALITOS2_MCR_SWR		0x1     /* s/w reset */
#define TALITOS_MCR_LO			0x1034
#define TALITOS_IMR			0x1008  /* interrupt mask register */
/* enable channel IRQs */
#define   TALITOS1_IMR_INIT		ISR1_FORMAT(0xf)
#define   TALITOS1_IMR_DONE		ISR1_FORMAT(0x5) /* done IRQs */
/* enable channel IRQs */
#define   TALITOS2_IMR_INIT		(ISR2_FORMAT(0xf) | 0x10000)
#define   TALITOS2_IMR_DONE		ISR1_FORMAT(0x5) /* done IRQs */
#define TALITOS_IMR_LO			0x100C
#define   TALITOS1_IMR_LO_INIT		0x2000000 /* allow RNGU error IRQs */
#define   TALITOS2_IMR_LO_INIT		0x20000 /* allow RNGU error IRQs */
#define TALITOS_ISR			0x1010  /* interrupt status register */
#define   TALITOS1_ISR_4CHERR		ISR1_FORMAT(0xa) /* 4 ch errors mask */
#define   TALITOS1_ISR_4CHDONE		ISR1_FORMAT(0x5) /* 4 ch done mask */
#define   TALITOS1_ISR_TEA_ERR		0x00000040
#define   TALITOS2_ISR_4CHERR		ISR2_FORMAT(0xa) /* 4 ch errors mask */
#define   TALITOS2_ISR_4CHDONE		ISR2_FORMAT(0x5) /* 4 ch done mask */
#define   TALITOS2_ISR_CH_0_2_ERR	ISR2_FORMAT(0x2) /* ch 0, 2 err mask */
#define   TALITOS2_ISR_CH_0_2_DONE	ISR2_FORMAT(0x1) /* ch 0, 2 done mask */
#define   TALITOS2_ISR_CH_1_3_ERR	ISR2_FORMAT(0x8) /* ch 1, 3 err mask */
#define   TALITOS2_ISR_CH_1_3_DONE	ISR2_FORMAT(0x4) /* ch 1, 3 done mask */
#define TALITOS_ISR_LO			0x1014
#define TALITOS_ICR			0x1018  /* interrupt clear register */
#define TALITOS_ICR_LO			0x101C

/* channel register address stride */
#define TALITOS_CH_BASE_OFFSET		0x1000	/* default channel map base */
#define TALITOS1_CH_STRIDE		0x1000
#define TALITOS2_CH_STRIDE		0x100

/* channel configuration register  */
#define TALITOS_CCCR			0x8
#define   TALITOS2_CCCR_CONT		0x2    /* channel continue on SEC2 */
#define   TALITOS2_CCCR_RESET		0x1    /* channel reset on SEC2 */
#define TALITOS_CCCR_LO			0xc
#define   TALITOS_CCCR_LO_IWSE		0x80   /* chan. ICCR writeback enab. */
#define   TALITOS_CCCR_LO_EAE		0x20   /* extended address enable */
#define   TALITOS_CCCR_LO_CDWE		0x10   /* chan. done writeback enab. */
#define   TALITOS_CCCR_LO_NT		0x4    /* notification type */
#define   TALITOS_CCCR_LO_CDIE		0x2    /* channel done IRQ enable */
#define   TALITOS1_CCCR_LO_RESET	0x1    /* channel reset on SEC1 */

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

/* execution unit registers base */
#define TALITOS2_DEU			0x2000
#define TALITOS2_AESU			0x4000
#define TALITOS2_MDEU			0x6000
#define TALITOS2_AFEU			0x8000
#define TALITOS2_RNGU			0xa000
#define TALITOS2_PKEU			0xc000
#define TALITOS2_KEU			0xe000
#define TALITOS2_CRCU			0xf000

#define TALITOS12_AESU			0x4000
#define TALITOS12_DEU			0x5000
#define TALITOS12_MDEU			0x6000

#define TALITOS10_AFEU			0x8000
#define TALITOS10_DEU			0xa000
#define TALITOS10_MDEU			0xc000
#define TALITOS10_RNGU			0xe000
#define TALITOS10_PKEU			0x10000
#define TALITOS10_AESU			0x12000

/* execution unit interrupt status registers */
#define TALITOS_EUDSR			0x10	/* data size */
#define TALITOS_EUDSR_LO		0x14
#define TALITOS_EURCR			0x18 /* reset control*/
#define TALITOS_EURCR_LO		0x1c
#define TALITOS_EUSR			0x28 /* rng status */
#define TALITOS_EUSR_LO			0x2c
#define TALITOS_EUISR			0x30
#define TALITOS_EUISR_LO		0x34
#define TALITOS_EUICR			0x38 /* int. control */
#define TALITOS_EUICR_LO		0x3c
#define TALITOS_EU_FIFO			0x800 /* output FIFO */
#define TALITOS_EU_FIFO_LO		0x804 /* output FIFO */
/* DES unit */
#define   TALITOS1_DEUICR_KPE		0x00200000 /* Key Parity Error */
/* message digest unit */
#define   TALITOS_MDEUICR_LO_ICE	0x4000 /* integrity check IRQ enable */
/* random number unit */
#define   TALITOS_RNGUSR_LO_RD		0x1	/* reset done */
#define   TALITOS_RNGUSR_LO_OFL		0xff0000/* output FIFO length */
#define   TALITOS_RNGURCR_LO_SR		0x1	/* software reset */

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
#define DESC_HDR_MODE1_MDEU_SHA224_HMAC	(DESC_HDR_MODE1_MDEU_SHA224 | \
					 DESC_HDR_MODE1_MDEU_HMAC)
#define DESC_HDR_MODE1_MDEUB_SHA384_HMAC	(DESC_HDR_MODE1_MDEUB_SHA384 | \
						 DESC_HDR_MODE1_MDEU_HMAC)
#define DESC_HDR_MODE1_MDEUB_SHA512_HMAC	(DESC_HDR_MODE1_MDEUB_SHA512 | \
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
