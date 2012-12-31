/*
 * Cryptographic API.
 *
 * Support for ACE (Advanced Crypto Engine) for S5PV210/EXYNOS4210.
 *
 * Copyright (c) 2011  Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/memory.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/hrtimer.h>

#include <asm/cacheflush.h>

#include <crypto/aes.h>
#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#include <crypto/scatterwalk.h>

#include <mach/secmem.h>

#include "ace.h"
#include "ace_sfr.h"

#define S5P_ACE_DRIVER_NAME		"s5p-ace"
#define ACE_AES_MIN_BLOCK_SIZE		16

#undef ACE_USE_ACP
#ifdef ACE_USE_ACP
#define PA_SSS_USER_CON			0x10010344
#define ACE_ARCACHE			0xA
#define ACE_AWCACHE			0xA
#endif

#undef ACE_DEBUG_HEARTBEAT
#undef ACE_DEBUG_WATCHDOG

#ifdef CONFIG_ACE_DEBUG
#define S5P_ACE_DEBUG(args...)		printk(KERN_INFO args)
#else
#define S5P_ACE_DEBUG(args...)
#endif

#define s5p_ace_read_sfr(_sfr_)		__raw_readl(s5p_ace_dev.ace_base + (_sfr_))
#define s5p_ace_write_sfr(_sfr_, _val_)	__raw_writel((_val_), s5p_ace_dev.ace_base + (_sfr_))

enum s5p_cpu_type {
	TYPE_S5PV210,
	TYPE_EXYNOS4,
};

enum {
	FLAGS_BC_BUSY,
	FLAGS_HASH_BUSY,
	FLAGS_SUSPENDED,
	FLAGS_USE_SW
};

static struct s5p_ace_device s5p_ace_dev;

#ifdef CONFIG_ACE_BC_ASYNC
static void s5p_ace_bc_task(unsigned long data);
#endif

#define ACE_CLOCK_ON		0
#define ACE_CLOCK_OFF		1

static int count_clk;
static int count_clk_delta;

#if defined(ACE_DEBUG_HEARTBEAT) || defined(ACE_DEBUG_WATCHDOG)
#define ACE_HEARTBEAT_MS		10000
#define ACE_WATCHDOG_MS			500

struct timeval timestamp_base;
struct timeval timestamp[5];

static inline void s5p_ace_dump(void)
{
	int i;
	char *str[] = {"request: ", "dma start: ", "dma end: ", "suspend: ", "resume: "};

	for (i = 0; i < 5; i++)
		printk(KERN_INFO "%s%5lu.%06lu\n",
			str[i], timestamp[i].tv_sec - timestamp_base.tv_sec, timestamp[i].tv_usec);
	printk(KERN_INFO "clock: [%d - %d]\n", count_clk, count_clk_delta);
}
#endif

struct s5p_ace_reqctx {
	u32				mode;
};

struct s5p_ace_device {
	void __iomem			*ace_base;
	struct clk			*clock;
#if defined(CONFIG_ACE_BC_IRQMODE) || defined(CONFIG_ACE_HASH_IRQMODE)
	int				irq;
#endif
#ifdef ACE_USE_ACP
	void __iomem			*sss_usercon;
#endif
	spinlock_t			lock;
	unsigned long			flags;

	struct hrtimer			timer;
	struct work_struct		work;
#ifdef ACE_DEBUG_HEARTBEAT
	struct hrtimer			heartbeat;
#endif
#ifdef ACE_DEBUG_WATCHDOG
	struct hrtimer			watchdog_bc;
#endif

#ifdef CONFIG_ACE_BC_ASYNC
	struct crypto_queue		queue_bc;
	struct tasklet_struct		task_bc;
	int				rc_depth_bc;
#endif

	struct s5p_ace_aes_ctx		*ctx_bc;

#ifdef CONFIG_ACE_HASH_ASYNC
	struct crypto_queue		queue_hash;
	struct tasklet_struct		task_hash;
#endif
	enum s5p_cpu_type		cputype;
};

#if defined(CONFIG_ACE_HASH_SHA1) || defined(CONFIG_ACE_HASH_SHA256)
struct crypto_shash *sw_tfm;
struct crypto_hash **fallback_hash;
#endif
struct secmem_crypto_driver_ftn secmem_ftn;

static void s5p_ace_init_clock_gating(void)
{
	count_clk = 0;
	count_clk_delta = 0;
}

static void s5p_ace_deferred_clock_disable(struct work_struct *work)
{
	unsigned long flags;
	int tmp;

	if (count_clk_delta == 0)
		return;

	spin_lock_irqsave(&s5p_ace_dev.lock, flags);
	count_clk -= count_clk_delta;
	count_clk_delta = 0;
	tmp = count_clk;
	spin_unlock_irqrestore(&s5p_ace_dev.lock, flags);

	if (tmp == 0) {
		clk_disable(s5p_ace_dev.clock);
		S5P_ACE_DEBUG("ACE clock OFF\n");
	}
}

static enum hrtimer_restart s5p_ace_timer_func(struct hrtimer *timer)
{
	S5P_ACE_DEBUG("ACE HRTIMER\n");

	/* It seems that "schedule_work" is expensive. */
	schedule_work(&s5p_ace_dev.work);

	return HRTIMER_NORESTART;
}

static void s5p_ace_clock_gating(int status)
{
	unsigned long flags;
	int tmp;

	if (status == ACE_CLOCK_ON) {
		spin_lock_irqsave(&s5p_ace_dev.lock, flags);
		tmp = count_clk++;
		spin_unlock_irqrestore(&s5p_ace_dev.lock, flags);

		if (tmp == 0) {
			clk_enable(s5p_ace_dev.clock);
			S5P_ACE_DEBUG("ACE clock ON\n");
		}
	} else if (status == ACE_CLOCK_OFF) {
		spin_lock_irqsave(&s5p_ace_dev.lock, flags);
		if (count_clk > 1)
			count_clk--;
		else
			count_clk_delta++;
		spin_unlock_irqrestore(&s5p_ace_dev.lock, flags);

		hrtimer_start(&s5p_ace_dev.timer,
			ns_to_ktime((u64)500 * NSEC_PER_MSEC),
			HRTIMER_MODE_REL);
	}
}

struct s5p_ace_aes_ctx {
	u32		keylen;

	u32		sfr_ctrl;
	u8		sfr_key[AES_MAX_KEY_SIZE];
	u8		sfr_semikey[AES_BLOCK_SIZE];

	struct crypto_blkcipher		*fallback_bc;
#ifdef CONFIG_ACE_BC_ASYNC
	struct ablkcipher_request	*req;
	struct crypto_ablkcipher	*fallback_abc;
	struct crypto_tfm		*origin_tfm;
#else
	struct crypto_blkcipher		*origin_tfm;

#endif
	size_t				total;
	struct scatterlist		*in_sg;
	size_t				in_ofs;
	struct scatterlist		*out_sg;
	size_t				out_ofs;

	int				directcall;

	u8				*src_addr;
	u8				*dst_addr;
	u32				dma_size;
	u8				tbuf[AES_BLOCK_SIZE];
};

#if defined(ACE_DEBUG_HEARTBEAT) || defined(ACE_DEBUG_WATCHDOG)
static void s5p_ace_print_info(void)
{
	struct s5p_ace_aes_ctx *sctx = s5p_ace_dev.ctx_bc;

	printk(KERN_INFO "flags: 0x%X\n", (u32)s5p_ace_dev.flags);
	s5p_ace_dump();
	if (sctx == NULL) {
		printk(KERN_INFO "sctx == NULL\n");
	} else {
#ifdef CONFIG_ACE_BC_ASYNC
		printk(KERN_INFO "sctx->req:      0x%08X\n", (u32)sctx->req);
#endif
		printk(KERN_INFO "sctx->total:    0x%08X\n", sctx->total);
		printk(KERN_INFO "sctx->dma_size: 0x%08X\n", sctx->dma_size);
	}
}
#endif

#ifdef ACE_DEBUG_HEARTBEAT
static enum hrtimer_restart s5p_ace_heartbeat_func(struct hrtimer *timer)
{
	printk(KERN_INFO "[[ACE HEARTBEAT]] -- START ----------\n");

	s5p_ace_print_info();

	printk(KERN_INFO "[[ACE HEARTBEAT]] -- END ------------\n");

	hrtimer_start(&s5p_ace_dev.heartbeat,
		ns_to_ktime((u64)ACE_HEARTBEAT_MS * NSEC_PER_MSEC),
		HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}
#endif

#ifdef ACE_DEBUG_WATCHDOG
static enum hrtimer_restart s5p_ace_watchdog_bc_func(struct hrtimer *timer)
{
	printk(KERN_ERR "[[ACE WATCHDOG BC]] ============\n");

	s5p_ace_print_info();

	return HRTIMER_NORESTART;
}
#endif

static void s5p_ace_resume_device(struct s5p_ace_device *dev)
{
	if (test_and_clear_bit(FLAGS_SUSPENDED, &dev->flags)) {
		clear_bit(FLAGS_BC_BUSY, &dev->flags);
		clear_bit(FLAGS_HASH_BUSY, &dev->flags);

#ifdef ACE_USE_ACP
		/* Set ARUSER[12:8] and AWUSER[4:0] */
		writel(0x101, dev->sss_usercon
			+ (PA_SSS_USER_CON & (PAGE_SIZE - 1)));
#endif
	}
}

static int s5p_ace_aes_set_cipher(struct s5p_ace_aes_ctx *sctx,
				u32 alg_id, u32 key_size)
{
	u32 new_status = 0;

	/* Fixed setting */
	new_status |= ACE_AES_FIFO_ON;

	if (s5p_ace_dev.cputype == TYPE_S5PV210)
		new_status |= ACE_AES_KEYCNGMODE_ON;

	new_status |= ACE_AES_SWAPKEY_ON;
	new_status |= ACE_AES_SWAPCNT_ON;
	new_status |= ACE_AES_SWAPIV_ON;

	if (s5p_ace_dev.cputype == TYPE_EXYNOS4) {
		new_status |= ACE_AES_SWAPDO_ON;
		new_status |= ACE_AES_SWAPDI_ON;
	}

	switch (MI_GET_MODE(alg_id)) {
	case _MODE_ECB_:
		new_status |= ACE_AES_OPERMODE_ECB;
		break;
	case _MODE_CBC_:
		new_status |= ACE_AES_OPERMODE_CBC;
		break;
	case _MODE_CTR_:
		new_status |= ACE_AES_OPERMODE_CTR;
		break;
	default:
		return -EINVAL;
	}

	switch (key_size) {
	case 128:
		new_status |= ACE_AES_KEYSIZE_128;
		break;
	case 192:
		new_status |= ACE_AES_KEYSIZE_192;
		break;
	case 256:
		new_status |= ACE_AES_KEYSIZE_256;
		break;
	default:
		return -EINVAL;
	}

	/* Set AES context */
	sctx->sfr_ctrl = new_status;
	sctx->keylen = key_size >> 3;

	return 0;
}

/*
 * enc: BC_MODE_ENC - encryption, BC_MODE_DEC - decryption
 */
static int s5p_ace_aes_set_encmode(struct s5p_ace_aes_ctx *sctx, u32 enc)
{
	u32 status = sctx->sfr_ctrl;
	u32 enc_mode = ACE_AES_MODE_ENC;

	if ((status & ACE_AES_OPERMODE_MASK) != ACE_AES_OPERMODE_CTR)
		enc_mode = (enc == BC_MODE_ENC ?
			ACE_AES_MODE_ENC : ACE_AES_MODE_DEC);

	sctx->sfr_ctrl = (status & ~ACE_AES_MODE_MASK) | enc_mode;

	return 0;
}

static int s5p_ace_aes_update_semikey(struct s5p_ace_aes_ctx *sctx,
					u8 *in, u8 *out, u32 len)
{
	u32 *addr = (u32 *)sctx->sfr_semikey;
	u32 tmp1, tmp2;

	switch (sctx->sfr_ctrl & ACE_AES_OPERMODE_MASK) {
	case ACE_AES_OPERMODE_ECB:
		break;
	case ACE_AES_OPERMODE_CBC:
		if ((sctx->sfr_ctrl & ACE_AES_MODE_MASK) == ACE_AES_MODE_ENC)
			memcpy(sctx->sfr_semikey, out, AES_BLOCK_SIZE);
		else
			memcpy(sctx->sfr_semikey, in, AES_BLOCK_SIZE);
		break;
	case ACE_AES_OPERMODE_CTR:
		tmp1 = be32_to_cpu(addr[3]);
		tmp2 = tmp1 + (len >> 4);
		addr[3] = be32_to_cpu(tmp2);
		if (tmp2 < tmp1) {
			tmp1 = be32_to_cpu(addr[2]) + 1;
			addr[2] = be32_to_cpu(tmp1);
			if (addr[2] == 0) {
				tmp1 = be32_to_cpu(addr[1]) + 1;
				addr[1] = be32_to_cpu(tmp1);
				if (addr[1] == 0) {
					tmp1 = be32_to_cpu(addr[0]) + 1;
					addr[0] = be32_to_cpu(tmp1);
				}
			}
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s5p_ace_aes_write_sfr(struct s5p_ace_aes_ctx *sctx)
{
	u32 *addr;

	s5p_ace_write_sfr(ACE_AES_CONTROL, sctx->sfr_ctrl);

	addr = (u32 *)sctx->sfr_key;
	switch (sctx->keylen) {
	case 16:
		s5p_ace_write_sfr(ACE_AES_KEY5, addr[0]);
		s5p_ace_write_sfr(ACE_AES_KEY6, addr[1]);
		s5p_ace_write_sfr(ACE_AES_KEY7, addr[2]);
		s5p_ace_write_sfr(ACE_AES_KEY8, addr[3]);
		break;
	case 24:
		s5p_ace_write_sfr(ACE_AES_KEY3, addr[0]);
		s5p_ace_write_sfr(ACE_AES_KEY4, addr[1]);
		s5p_ace_write_sfr(ACE_AES_KEY5, addr[2]);
		s5p_ace_write_sfr(ACE_AES_KEY6, addr[3]);
		s5p_ace_write_sfr(ACE_AES_KEY7, addr[4]);
		s5p_ace_write_sfr(ACE_AES_KEY8, addr[5]);
		break;
	case 32:
		s5p_ace_write_sfr(ACE_AES_KEY1, addr[0]);
		s5p_ace_write_sfr(ACE_AES_KEY2, addr[1]);
		s5p_ace_write_sfr(ACE_AES_KEY3, addr[2]);
		s5p_ace_write_sfr(ACE_AES_KEY4, addr[3]);
		s5p_ace_write_sfr(ACE_AES_KEY5, addr[4]);
		s5p_ace_write_sfr(ACE_AES_KEY6, addr[5]);
		s5p_ace_write_sfr(ACE_AES_KEY7, addr[6]);
		s5p_ace_write_sfr(ACE_AES_KEY8, addr[7]);
		break;
	default:
		return -EINVAL;
	}

	addr = (u32 *)sctx->sfr_semikey;
	switch (sctx->sfr_ctrl & ACE_AES_OPERMODE_MASK) {
	case ACE_AES_OPERMODE_ECB:
		break;
	case ACE_AES_OPERMODE_CBC:
		s5p_ace_write_sfr(ACE_AES_IV1, addr[0]);
		s5p_ace_write_sfr(ACE_AES_IV2, addr[1]);
		s5p_ace_write_sfr(ACE_AES_IV3, addr[2]);
		s5p_ace_write_sfr(ACE_AES_IV4, addr[3]);
		break;
	case ACE_AES_OPERMODE_CTR:
		s5p_ace_write_sfr(ACE_AES_CNT1, addr[0]);
		s5p_ace_write_sfr(ACE_AES_CNT2, addr[1]);
		s5p_ace_write_sfr(ACE_AES_CNT3, addr[2]);
		s5p_ace_write_sfr(ACE_AES_CNT4, addr[3]);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s5p_ace_aes_engine_start(struct s5p_ace_aes_ctx *sctx,
				u8 *out, const u8 *in, u32 len, int irqen)
{
	u32 reg;
	u32 first_blklen;

	if ((sctx == NULL) || (out == NULL) || (in == NULL)) {
		printk(KERN_ERR "%s : NULL input.\n", __func__);
		return -EINVAL;
	}

	if (len & (AES_BLOCK_SIZE - 1)) {
		printk(KERN_ERR "Invalid len for AES engine (%d)\n", len);
		return -EINVAL;
	}

	if (s5p_ace_aes_write_sfr(sctx) != 0)
		return -EINVAL;

	S5P_ACE_DEBUG("AES: %s, in: 0x%08X, out: 0x%08X, len: 0x%08X\n",
			__func__, (u32)in, (u32)out, len);
	S5P_ACE_DEBUG("AES: %s, AES_control : 0x%08X\n",
			__func__, s5p_ace_read_sfr(ACE_AES_CONTROL));

	/* Assert code */
	reg = s5p_ace_read_sfr(ACE_AES_STATUS);
	if ((reg & ACE_AES_BUSY_MASK) == ACE_AES_BUSY_ON)
		return -EBUSY;

	/* Flush BRDMA and BTDMA */
	s5p_ace_write_sfr(ACE_FC_BRDMAC, ACE_FC_BRDMACFLUSH_ON);
	s5p_ace_write_sfr(ACE_FC_BTDMAC, ACE_FC_BTDMACFLUSH_ON);

	/* Select Input MUX as AES */
	reg = s5p_ace_read_sfr(ACE_FC_FIFOCTRL);
	reg = (reg & ~ACE_FC_SELBC_MASK) | ACE_FC_SELBC_AES;
	s5p_ace_write_sfr(ACE_FC_FIFOCTRL, reg);

	/* Stop flushing BRDMA and BTDMA */
	reg = ACE_FC_BRDMACFLUSH_OFF;
	if (s5p_ace_dev.cputype == TYPE_S5PV210)
		reg |= ACE_FC_BRDMACSWAP_ON;

#ifdef ACE_USE_ACP
	reg |= ACE_ARCACHE << ACE_FC_BRDMACARCACHE_OFS;
#endif
	s5p_ace_write_sfr(ACE_FC_BRDMAC, reg);
	reg = ACE_FC_BTDMACFLUSH_OFF;
	if (s5p_ace_dev.cputype == TYPE_S5PV210)
		reg |= ACE_FC_BTDMACSWAP_ON;

#ifdef ACE_USE_ACP
	reg |= ACE_AWCACHE << ACE_FC_BTDMACAWCACHE_OFS;
#endif
	s5p_ace_write_sfr(ACE_FC_BTDMAC, reg);

	/* Set DMA */
	s5p_ace_write_sfr(ACE_FC_BRDMAS, (u32)in);
	s5p_ace_write_sfr(ACE_FC_BTDMAS, (u32)out);

	if (s5p_ace_dev.cputype == TYPE_S5PV210) {
		/* Set the length of first block (Key Change Mode On) */
		if ((((u32)in) & (2 * AES_BLOCK_SIZE - 1)) == 0)
			first_blklen = 2 * AES_BLOCK_SIZE;
		else
			first_blklen = AES_BLOCK_SIZE;

		if (len <= first_blklen) {
#ifdef CONFIG_ACE_BC_IRQMODE
			if (irqen)
				s5p_ace_write_sfr(ACE_FC_INTENSET, ACE_FC_BTDMA);
#endif

			/* Set DMA */
			s5p_ace_write_sfr(ACE_FC_BRDMAL, len);
			s5p_ace_write_sfr(ACE_FC_BTDMAL, len);
		} else {
			unsigned long timeout;

			/* Set DMA */
			s5p_ace_write_sfr(ACE_FC_BRDMAL, first_blklen);
			s5p_ace_write_sfr(ACE_FC_BTDMAL, first_blklen);

			timeout = jiffies + msecs_to_jiffies(10);
			while (time_before(jiffies, timeout)) {
				if (s5p_ace_read_sfr(ACE_FC_INTPEND) & ACE_FC_BTDMA)
					break;
			}
			if (!(s5p_ace_read_sfr(ACE_FC_INTPEND) & ACE_FC_BTDMA)) {
				printk(KERN_ERR "AES : DMA time out\n");
				return -EBUSY;
			}
			s5p_ace_write_sfr(ACE_FC_INTPEND, ACE_FC_BTDMA | ACE_FC_BRDMA);

			reg = sctx->sfr_ctrl;
			reg = (reg & ~ACE_AES_KEYCNGMODE_MASK) | ACE_AES_KEYCNGMODE_OFF;
			s5p_ace_write_sfr(ACE_AES_CONTROL, reg);

#ifdef CONFIG_ACE_BC_IRQMODE
			if (irqen)
				s5p_ace_write_sfr(ACE_FC_INTENSET, ACE_FC_BTDMA);
#endif

			/* Set DMA */
			s5p_ace_write_sfr(ACE_FC_BRDMAL, len - first_blklen);
			s5p_ace_write_sfr(ACE_FC_BTDMAL, len - first_blklen);
		}
	} else {
#ifdef CONFIG_ACE_BC_IRQMODE
		if (irqen)
			s5p_ace_write_sfr(ACE_FC_INTENSET, ACE_FC_BTDMA);
#endif

		/* Set DMA */
		s5p_ace_write_sfr(ACE_FC_BRDMAL, len);
		s5p_ace_write_sfr(ACE_FC_BTDMAL, len);
	}

	return 0;
}

static void s5p_ace_aes_engine_wait(struct s5p_ace_aes_ctx *sctx,
				u8 *out, const u8 *in, u32 len)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(10);
	while (time_before(jiffies, timeout))
		if (s5p_ace_read_sfr(ACE_FC_INTPEND) & ACE_FC_BTDMA)
			break;
	if (!(s5p_ace_read_sfr(ACE_FC_INTPEND) & ACE_FC_BTDMA))
		printk(KERN_ERR "%s : DMA time out\n", __func__);
	s5p_ace_write_sfr(ACE_FC_INTPEND, ACE_FC_BTDMA | ACE_FC_BRDMA);
}

void s5p_ace_sg_update(struct scatterlist **sg, size_t *offset,
					size_t count)
{
	*offset += count;
	if (*offset >= sg_dma_len(*sg)) {
		*offset -= sg_dma_len(*sg);
		*sg = sg_next(*sg);
	}
}

int s5p_ace_sg_set_from_sg(struct scatterlist *dst, struct scatterlist *src,
			u32 num)
{
	sg_init_table(dst, num);
	while (num--) {
		sg_set_page(dst, sg_page(src), sg_dma_len(src), src->offset);

		src = sg_next(src);
		dst = sg_next(dst);
		if (!src || !dst)
			return -ENOMEM;
	}
	return 0;
}

/* Unaligned data Handling
 * - size should be a multiple of ACE_AES_MIN_BLOCK_SIZE.
 */
static int s5p_ace_aes_crypt_unaligned(struct s5p_ace_aes_ctx *sctx,
					size_t size)
{
	struct blkcipher_desc desc;
	struct scatterlist in_sg[2], out_sg[2];
	int ret;

	S5P_ACE_DEBUG("%s - %s (size: %d / %d)\n", __func__,
		sctx->fallback_bc->base.__crt_alg->cra_driver_name,
		size, sctx->total);

	desc.tfm = sctx->fallback_bc;
	desc.info = sctx->sfr_semikey;
	desc.flags = 0;

	s5p_ace_sg_set_from_sg(in_sg, sctx->in_sg, 2);
	in_sg->length -= sctx->in_ofs;
	in_sg->offset += sctx->in_ofs;

	s5p_ace_sg_set_from_sg(out_sg, sctx->out_sg, 2);
	out_sg->length -= sctx->out_ofs;
	out_sg->offset += sctx->out_ofs;

	if ((sctx->sfr_ctrl & ACE_AES_MODE_MASK) == ACE_AES_MODE_ENC)
		ret = crypto_blkcipher_encrypt_iv(
				&desc, out_sg, in_sg, size);
	else
		ret = crypto_blkcipher_decrypt_iv(
				&desc, out_sg, in_sg, size);

	sctx->dma_size = 0;
	sctx->total -= size;
	if (!sctx->total)
		return 0;

	s5p_ace_sg_update(&sctx->in_sg, &sctx->in_ofs, size);
	s5p_ace_sg_update(&sctx->out_sg, &sctx->out_ofs, size);

	return 0;
}

static int s5p_ace_aes_crypt_dma_start(struct s5p_ace_device *dev)
{
	struct s5p_ace_aes_ctx *sctx = dev->ctx_bc;
	u8 *src, *dst;
	size_t count;
	int i;
	int ret;

#if defined(ACE_DEBUG_HEARTBEAT) || defined(ACE_DEBUG_WATCHDOG)
	do_gettimeofday(&timestamp[1]);		/* 1: dma start */
#endif

	sctx->directcall = 0;

	while (1) {
		count = sctx->total;
		count = min(count, sg_dma_len(sctx->in_sg) - sctx->in_ofs);
		count = min(count, sg_dma_len(sctx->out_sg) - sctx->out_ofs);

		S5P_ACE_DEBUG("total_start: %d (%d)\n", sctx->total, count);
		S5P_ACE_DEBUG(" in(ofs: %x, len: %x), %x\n",
				sctx->in_sg->offset, sg_dma_len(sctx->in_sg),
				sctx->in_ofs);
		S5P_ACE_DEBUG(" out(ofs: %x, len: %x), %x\n",
				sctx->out_sg->offset, sg_dma_len(sctx->out_sg),
				sctx->out_ofs);

		if (count > ACE_AES_MIN_BLOCK_SIZE)
			break;

		count = min(sctx->total, (size_t)ACE_AES_MIN_BLOCK_SIZE);
		if (count & (AES_BLOCK_SIZE - 1))
			printk(KERN_ERR "%s - Invalid count\n", __func__);
		ret = s5p_ace_aes_crypt_unaligned(sctx, count);
		if (!sctx->total) {
#if defined(ACE_DEBUG_HEARTBEAT) || defined(ACE_DEBUG_WATCHDOG)
			do_gettimeofday(&timestamp[2]);		/* 2: dma end */
#endif
#ifdef CONFIG_ACE_BC_IRQMODE
			tasklet_schedule(&dev->task_bc);
			return 0;
#else
			goto run;
#endif
		}
	}

	count &= ~(AES_BLOCK_SIZE - 1);
	sctx->dma_size = count;

	src = (u8 *)page_to_phys(sg_page(sctx->in_sg));
	src += sctx->in_sg->offset + sctx->in_ofs;
	if (!PageHighMem(sg_page(sctx->in_sg))) {
		sctx->src_addr = (u8 *)phys_to_virt((u32)src);
	} else {
		sctx->src_addr = crypto_kmap(sg_page(sctx->in_sg),
						crypto_kmap_type(0));
		sctx->src_addr += sctx->in_sg->offset + sctx->in_ofs;
	}

	dst = (u8 *)page_to_phys(sg_page(sctx->out_sg));
	dst += sctx->out_sg->offset + sctx->out_ofs;
	if (!PageHighMem(sg_page(sctx->out_sg))) {
		sctx->dst_addr = (u8 *)phys_to_virt((u32)dst);
	} else {
		sctx->dst_addr = crypto_kmap(sg_page(sctx->out_sg),
						crypto_kmap_type(1));
		sctx->dst_addr += sctx->out_sg->offset + sctx->out_ofs;
	}

	S5P_ACE_DEBUG("  phys(src: %x, dst: %x)\n", (u32)src, (u32)dst);
	S5P_ACE_DEBUG("  virt(src: %x, dst: %x)\n",
		(u32)sctx->src_addr, (u32)sctx->dst_addr);

	if (src == dst)
		memcpy(sctx->tbuf, sctx->src_addr + count - AES_BLOCK_SIZE,
			AES_BLOCK_SIZE);

#ifndef ACE_USE_ACP
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
	dmac_clean_range((void *)sctx->src_addr,
		(void *)sctx->src_addr + count);
	dmac_clean_range((void *)sctx->dst_addr,
		(void *)sctx->dst_addr + count);
#else
	dmac_map_area((void *)sctx->src_addr, count, DMA_TO_DEVICE);
	outer_clean_range((unsigned long)src, (unsigned long)src + count);
	dmac_map_area((void *)sctx->dst_addr, count, DMA_FROM_DEVICE);
	outer_clean_range((unsigned long)dst, (unsigned long)dst + count);
#endif
#endif

	for (i = 0; i < 100; i++) {
		ret = s5p_ace_aes_engine_start(sctx, dst, src, count, 1);
		if (ret != -EBUSY)
			break;
	}
	if (i == 100) {
		printk(KERN_ERR "%s : DMA Start Failed\n", __func__);
		return ret;
	}

run:
#ifdef CONFIG_ACE_BC_ASYNC
#ifndef CONFIG_ACE_BC_IRQMODE
	if (!ret) {
		if ((count <= 2048) && ((s5p_ace_dev.rc_depth_bc++) < 1)) {
			sctx->directcall = 1;
			s5p_ace_bc_task((unsigned long)&s5p_ace_dev);
			return ret;
		}
	}
#endif

	if (sctx->dma_size) {
		if (PageHighMem(sg_page(sctx->in_sg)))
			crypto_kunmap(sctx->src_addr, crypto_kmap_type(0));
		if (PageHighMem(sg_page(sctx->out_sg)))
			crypto_kunmap(sctx->dst_addr, crypto_kmap_type(1));
	}

#ifndef CONFIG_ACE_BC_IRQMODE
	if (!ret)
		tasklet_schedule(&dev->task_bc);
#endif
#endif
	return ret;
}

static int s5p_ace_aes_crypt_dma_wait(struct s5p_ace_device *dev)
{
	struct s5p_ace_aes_ctx *sctx = dev->ctx_bc;
	u8 *src, *dst;
	u8 *src_lb_addr;
	u32 lastblock;
	int ret = 0;

	S5P_ACE_DEBUG("%s\n", __func__);

	src = (u8 *)page_to_phys(sg_page(sctx->in_sg));
	src += sctx->in_sg->offset + sctx->in_ofs;
	dst = (u8 *)page_to_phys(sg_page(sctx->out_sg));
	dst += sctx->out_sg->offset + sctx->out_ofs;

#ifdef CONFIG_ACE_BC_ASYNC
	if (!sctx->directcall) {
		if (PageHighMem(sg_page(sctx->in_sg))) {
			sctx->src_addr = crypto_kmap(sg_page(sctx->in_sg),
							crypto_kmap_type(0));
			sctx->src_addr += sctx->in_sg->offset + sctx->in_ofs;
		}

		if (PageHighMem(sg_page(sctx->out_sg))) {
			sctx->dst_addr = crypto_kmap(sg_page(sctx->out_sg),
							crypto_kmap_type(1));
			sctx->dst_addr += sctx->out_sg->offset + sctx->out_ofs;
		}
	}
#endif

#ifndef CONFIG_ACE_BC_IRQMODE
	s5p_ace_aes_engine_wait(sctx, dst, src, sctx->dma_size);
#endif

#ifndef ACE_USE_ACP
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
	dmac_inv_range((void *)sctx->dst_addr,
		(void *)sctx->dst_addr + sctx->dma_size);
#else
	dmac_unmap_area((void *)sctx->dst_addr, sctx->dma_size,
		DMA_FROM_DEVICE);
	outer_inv_range((unsigned long)dst,
		(unsigned long)dst + sctx->dma_size);
#endif
#endif

	lastblock = sctx->dma_size - AES_BLOCK_SIZE;
	if (src == dst)
		src_lb_addr = sctx->tbuf;
	else
		src_lb_addr = sctx->src_addr + lastblock;
	if (s5p_ace_aes_update_semikey(sctx, src_lb_addr,
				sctx->dst_addr + lastblock,
				sctx->dma_size) != 0)
		return -EINVAL;

	if (PageHighMem(sg_page(sctx->in_sg)))
		crypto_kunmap(sctx->src_addr, crypto_kmap_type(0));
	if (PageHighMem(sg_page(sctx->out_sg)))
		crypto_kunmap(sctx->dst_addr, crypto_kmap_type(1));

	sctx->total -= sctx->dma_size;

	S5P_ACE_DEBUG("total_end: %d\n", sctx->total);

	if (ret || !sctx->total) {
		if (ret)
			printk(KERN_NOTICE "err: %d\n", ret);
	} else {
		s5p_ace_sg_update(&sctx->in_sg, &sctx->in_ofs,
					sctx->dma_size);
		s5p_ace_sg_update(&sctx->out_sg, &sctx->out_ofs,
					sctx->dma_size);
	}

#if defined(ACE_DEBUG_HEARTBEAT) || defined(ACE_DEBUG_WATCHDOG)
	do_gettimeofday(&timestamp[2]);		/* 2: dma end */
#endif

	return ret;
}

#ifdef CONFIG_ACE_BC_ASYNC
static int s5p_ace_handle_lock_req(struct s5p_ace_device *dev,
				struct s5p_ace_aes_ctx *sctx,
				struct ablkcipher_request *req, u32 encmode)
{
	int ret;

	sctx->origin_tfm = req->base.tfm;
	crypto_ablkcipher_set_flags(sctx->fallback_abc, 0);
	ablkcipher_request_set_tfm(req, sctx->fallback_abc);

	if (encmode == BC_MODE_ENC)
		ret = crypto_ablkcipher_encrypt(req);
	else
		ret = crypto_ablkcipher_decrypt(req);

	if (!test_and_set_bit(FLAGS_BC_BUSY, &s5p_ace_dev.flags)) {
		sctx->req = req;
		dev->ctx_bc = sctx;
		tasklet_schedule(&dev->task_bc);
	} else {
		req->base.tfm = sctx->origin_tfm;
		req->base.complete(&req->base, ret);
		s5p_ace_clock_gating(ACE_CLOCK_OFF);
	}

	return ret;
}

static int s5p_ace_aes_handle_req(struct s5p_ace_device *dev)
{
	struct crypto_async_request *async_req;
	struct crypto_async_request *backlog;
	struct s5p_ace_aes_ctx *sctx;
	struct s5p_ace_reqctx *rctx;
	struct ablkcipher_request *req;
	unsigned long flags;

	if (dev->ctx_bc)
		goto start;

	S5P_ACE_DEBUG("%s\n", __func__);

	spin_lock_irqsave(&s5p_ace_dev.lock, flags);
	backlog = crypto_get_backlog(&dev->queue_bc);
	async_req = crypto_dequeue_request(&dev->queue_bc);
	S5P_ACE_DEBUG("[[ dequeue (%u) ]]\n", dev->queue_bc.qlen);
	spin_unlock_irqrestore(&s5p_ace_dev.lock, flags);

	if (!async_req) {
		clear_bit(FLAGS_BC_BUSY, &dev->flags);
		s5p_ace_clock_gating(ACE_CLOCK_OFF);
		return 0;
	}

	if (backlog) {
		S5P_ACE_DEBUG("backlog.\n");
		backlog->complete(backlog, -EINPROGRESS);
	}

	S5P_ACE_DEBUG("get new req\n");

	req = ablkcipher_request_cast(async_req);
	sctx = crypto_ablkcipher_ctx(crypto_ablkcipher_reqtfm(req));

#ifdef ACE_DEBUG_WATCHDOG
	hrtimer_start(&s5p_ace_dev.watchdog_bc,
		ns_to_ktime((u64)ACE_WATCHDOG_MS * NSEC_PER_MSEC),
		HRTIMER_MODE_REL);
#endif
	rctx = ablkcipher_request_ctx(req);

	if (s5p_ace_dev.flags & BIT_MASK(FLAGS_USE_SW)) {
		clear_bit(FLAGS_BC_BUSY, &s5p_ace_dev.flags);
		return s5p_ace_handle_lock_req(dev, sctx, req, rctx->mode);
	}

	/* assign new request to device */
	sctx->req = req;
	sctx->total = req->nbytes;
	sctx->in_sg = req->src;
	sctx->in_ofs = 0;
	sctx->out_sg = req->dst;
	sctx->out_ofs = 0;

	if ((sctx->sfr_ctrl & ACE_AES_OPERMODE_MASK) != ACE_AES_OPERMODE_ECB)
		memcpy(sctx->sfr_semikey, req->info, AES_BLOCK_SIZE);

	s5p_ace_aes_set_encmode(sctx, rctx->mode);

	dev->ctx_bc = sctx;

start:
	return s5p_ace_aes_crypt_dma_start(dev);
}

static void s5p_ace_bc_task(unsigned long data)
{
	struct s5p_ace_device *dev = (struct s5p_ace_device *)data;
	struct s5p_ace_aes_ctx *sctx = dev->ctx_bc;
	int ret = 0;

	S5P_ACE_DEBUG("%s (total: %d, dma_size: %d)\n", __func__,
				sctx->total, sctx->dma_size);

	/* check if it is handled by SW or HW */
	if (sctx->req->base.tfm ==
		crypto_ablkcipher_tfm
		(crypto_ablkcipher_crt(sctx->fallback_abc)->base)) {
		sctx->req->base.tfm = sctx->origin_tfm;
		sctx->req->base.complete(&sctx->req->base, ret);
		dev->ctx_bc = NULL;
		s5p_ace_aes_handle_req(dev);

		return;
	}

	if (sctx->dma_size)
		ret = s5p_ace_aes_crypt_dma_wait(dev);

	if (!sctx->total) {
		if ((sctx->sfr_ctrl & ACE_AES_OPERMODE_MASK)
				!= ACE_AES_OPERMODE_ECB)
			memcpy(sctx->req->info, sctx->sfr_semikey,
				AES_BLOCK_SIZE);
		sctx->req->base.complete(&sctx->req->base, ret);
		dev->ctx_bc = NULL;

#ifdef ACE_DEBUG_WATCHDOG
		hrtimer_cancel(&s5p_ace_dev.watchdog_bc);
#endif
	}

	s5p_ace_aes_handle_req(dev);
}

static int s5p_ace_aes_crypt(struct ablkcipher_request *req, u32 encmode)
{
	struct s5p_ace_reqctx *rctx = ablkcipher_request_ctx(req);
	unsigned long flags;
	int ret;
	unsigned long timeout;

#ifdef ACE_DEBUG_WATCHDOG
	do_gettimeofday(&timestamp[0]);		/* 0: request */
#endif

	S5P_ACE_DEBUG("%s (nbytes: 0x%x, mode: 0x%x)\n",
				__func__, (u32)req->nbytes, encmode);

	rctx->mode = encmode;

	timeout = jiffies + msecs_to_jiffies(10);
	while (time_before(jiffies, timeout)) {
		if (s5p_ace_dev.queue_bc.list.prev != &req->base.list)
			break;
		udelay(1);	/* wait */
	}
	if (s5p_ace_dev.queue_bc.list.prev == &req->base.list) {
		printk(KERN_ERR "%s : Time Out.\n", __func__);
		return -EAGAIN;
	}

	spin_lock_irqsave(&s5p_ace_dev.lock, flags);
	ret = ablkcipher_enqueue_request(&s5p_ace_dev.queue_bc, req);
	spin_unlock_irqrestore(&s5p_ace_dev.lock, flags);

	S5P_ACE_DEBUG("[[ enqueue (%u) ]]\n", s5p_ace_dev.queue_bc.qlen);

	s5p_ace_resume_device(&s5p_ace_dev);
	if (!test_and_set_bit(FLAGS_BC_BUSY, &s5p_ace_dev.flags)) {
		s5p_ace_clock_gating(ACE_CLOCK_ON);
		s5p_ace_dev.rc_depth_bc = 0;
		s5p_ace_aes_handle_req(&s5p_ace_dev);
	}

	return ret;
}
#else
static int s5p_ace_handle_lock_req(struct s5p_ace_aes_ctx *sctx,
				struct blkcipher_desc *desc,
				struct scatterlist *sg_dst,
				struct scatterlist *sg_src,
				unsigned int size, int encmode)
{
	int ret;

	sctx->origin_tfm = desc->tfm;
	desc->tfm = sctx->fallback_bc;

	if (encmode == BC_MODE_ENC)
		ret = crypto_blkcipher_encrypt_iv(desc, sg_dst, sg_src, size);
	else
		ret = crypto_blkcipher_decrypt_iv(desc, sg_dst, sg_src, size);

	desc->tfm = sctx->origin_tfm;

	return ret;
}

static int s5p_ace_aes_crypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes, int encmode)
{
	struct s5p_ace_aes_ctx *sctx = crypto_blkcipher_ctx(desc->tfm);
	int ret;

#if defined(ACE_DEBUG_HEARTBEAT) || defined(ACE_DEBUG_WATCHDOG)
	do_gettimeofday(&timestamp[0]);		/* 0: request */
#endif

#ifdef ACE_DEBUG_WATCHDOG
	hrtimer_start(&s5p_ace_dev.watchdog_bc,
		ns_to_ktime((u64)ACE_WATCHDOG_MS * NSEC_PER_MSEC),
		HRTIMER_MODE_REL);
#endif

	sctx->total = nbytes;
	sctx->in_sg = src;
	sctx->in_ofs = 0;
	sctx->out_sg = dst;
	sctx->out_ofs = 0;

	if ((sctx->sfr_ctrl & ACE_AES_OPERMODE_MASK) != ACE_AES_OPERMODE_ECB)
		memcpy(sctx->sfr_semikey, desc->info, AES_BLOCK_SIZE);

	s5p_ace_aes_set_encmode(sctx, encmode);

	s5p_ace_resume_device(&s5p_ace_dev);
	s5p_ace_clock_gating(ACE_CLOCK_ON);
	local_bh_disable();
	while (test_and_set_bit(FLAGS_BC_BUSY, &s5p_ace_dev.flags))
		udelay(1);

	if (s5p_ace_dev.flags & BIT_MASK(FLAGS_USE_SW)) {
		clear_bit(FLAGS_BC_BUSY, &s5p_ace_dev.flags);
		local_bh_enable();
		return s5p_ace_handle_lock_req(sctx, desc, dst, src, nbytes,
						encmode);
	}

	s5p_ace_dev.ctx_bc = sctx;

	do {
		ret = s5p_ace_aes_crypt_dma_start(&s5p_ace_dev);

		if (sctx->dma_size)
			ret = s5p_ace_aes_crypt_dma_wait(&s5p_ace_dev);
	} while (sctx->total);

	s5p_ace_dev.ctx_bc = NULL;

	clear_bit(FLAGS_BC_BUSY, &s5p_ace_dev.flags);
	local_bh_enable();
	s5p_ace_clock_gating(ACE_CLOCK_OFF);

	if ((sctx->sfr_ctrl & ACE_AES_OPERMODE_MASK) != ACE_AES_OPERMODE_ECB)
		memcpy(desc->info, sctx->sfr_semikey, AES_BLOCK_SIZE);

#ifdef ACE_DEBUG_WATCHDOG
	hrtimer_cancel(&s5p_ace_dev.watchdog_bc);
#endif

	return ret;
}
#endif

static int s5p_ace_aes_set_key(struct s5p_ace_aes_ctx *sctx, const u8 *key,
		unsigned int key_len)
{
	memcpy(sctx->sfr_key, key, key_len);
	crypto_blkcipher_setkey(sctx->fallback_bc, key, key_len);

#ifdef CONFIG_ACE_BC_ASYNC
	crypto_ablkcipher_setkey(sctx->fallback_abc, key, key_len);
#endif

	return 0;
}

#ifdef CONFIG_ACE_BC_ASYNC
static int s5p_ace_ecb_aes_set_key(struct crypto_ablkcipher *tfm, const u8 *key,
		unsigned int key_len)
{
	struct s5p_ace_aes_ctx *sctx = crypto_ablkcipher_ctx(tfm);
	s5p_ace_aes_set_cipher(sctx, MI_AES_ECB, key_len * 8);
	return s5p_ace_aes_set_key(sctx, key, key_len);
}

static int s5p_ace_cbc_aes_set_key(struct crypto_ablkcipher *tfm, const u8 *key,
		unsigned int key_len)
{
	struct s5p_ace_aes_ctx *sctx = crypto_ablkcipher_ctx(tfm);
	s5p_ace_aes_set_cipher(sctx, MI_AES_CBC, key_len * 8);
	return s5p_ace_aes_set_key(sctx, key, key_len);
}

static int s5p_ace_ctr_aes_set_key(struct crypto_ablkcipher *tfm, const u8 *key,
		unsigned int key_len)
{
	struct s5p_ace_aes_ctx *sctx = crypto_ablkcipher_ctx(tfm);
	s5p_ace_aes_set_cipher(sctx, MI_AES_CTR, key_len * 8);
	return s5p_ace_aes_set_key(sctx, key, key_len);
}

static int s5p_ace_ecb_aes_encrypt(struct ablkcipher_request *req)
{
	return s5p_ace_aes_crypt(req, BC_MODE_ENC);
}

static int s5p_ace_ecb_aes_decrypt(struct ablkcipher_request *req)
{
	return s5p_ace_aes_crypt(req, BC_MODE_DEC);
}

static int s5p_ace_cbc_aes_encrypt(struct ablkcipher_request *req)
{
	return s5p_ace_aes_crypt(req, BC_MODE_ENC);
}

static int s5p_ace_cbc_aes_decrypt(struct ablkcipher_request *req)
{
	return s5p_ace_aes_crypt(req, BC_MODE_DEC);
}

static int s5p_ace_ctr_aes_encrypt(struct ablkcipher_request *req)
{
	return s5p_ace_aes_crypt(req, BC_MODE_ENC);
}

static int s5p_ace_ctr_aes_decrypt(struct ablkcipher_request *req)
{
	return s5p_ace_aes_crypt(req, BC_MODE_DEC);
}
#else
static int s5p_ace_ecb_aes_set_key(struct crypto_tfm *tfm, const u8 *key,
		unsigned int key_len)
{
	struct s5p_ace_aes_ctx *sctx = crypto_tfm_ctx(tfm);
	s5p_ace_aes_set_cipher(sctx, MI_AES_ECB, key_len * 8);
	return s5p_ace_aes_set_key(sctx, key, key_len);
}

static int s5p_ace_cbc_aes_set_key(struct crypto_tfm *tfm, const u8 *key,
		unsigned int key_len)
{
	struct s5p_ace_aes_ctx *sctx = crypto_tfm_ctx(tfm);
	s5p_ace_aes_set_cipher(sctx, MI_AES_CBC, key_len * 8);
	return s5p_ace_aes_set_key(sctx, key, key_len);
}

static int s5p_ace_ctr_aes_set_key(struct crypto_tfm *tfm, const u8 *key,
		unsigned int key_len)
{
	struct s5p_ace_aes_ctx *sctx = crypto_tfm_ctx(tfm);
	s5p_ace_aes_set_cipher(sctx, MI_AES_CTR, key_len * 8);
	return s5p_ace_aes_set_key(sctx, key, key_len);
}

static int s5p_ace_ecb_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	return s5p_ace_aes_crypt(desc, dst, src, nbytes, BC_MODE_ENC);
}

static int s5p_ace_ecb_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	return s5p_ace_aes_crypt(desc, dst, src, nbytes, BC_MODE_DEC);
}

static int s5p_ace_cbc_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	return s5p_ace_aes_crypt(desc, dst, src, nbytes, BC_MODE_ENC);
}

static int s5p_ace_cbc_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	return s5p_ace_aes_crypt(desc, dst, src, nbytes, BC_MODE_DEC);
}

static int s5p_ace_ctr_aes_encrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	return s5p_ace_aes_crypt(desc, dst, src, nbytes, BC_MODE_ENC);
}

static int s5p_ace_ctr_aes_decrypt(struct blkcipher_desc *desc,
			   struct scatterlist *dst, struct scatterlist *src,
			   unsigned int nbytes)
{
	return s5p_ace_aes_crypt(desc, dst, src, nbytes, BC_MODE_DEC);
}
#endif

static int s5p_ace_cra_init_tfm(struct crypto_tfm *tfm)
{
	const char *name = tfm->__crt_alg->cra_name;
	struct s5p_ace_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	sctx->fallback_bc = crypto_alloc_blkcipher(name, 0,
			CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(sctx->fallback_bc)) {
		printk(KERN_ERR "Error allocating fallback algo %s\n", name);
		return PTR_ERR(sctx->fallback_bc);
	}
#ifdef CONFIG_ACE_BC_ASYNC
	tfm->crt_ablkcipher.reqsize = sizeof(struct s5p_ace_reqctx);
	sctx->fallback_abc = crypto_alloc_ablkcipher(name, 0,
			CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(sctx->fallback_abc)) {
		printk(KERN_ERR "Error allocating abc fallback algo %s\n",
			name);
		return PTR_ERR(sctx->fallback_abc);
	}

#endif
	S5P_ACE_DEBUG("%s\n", __func__);

	return 0;
}

static void s5p_ace_cra_exit_tfm(struct crypto_tfm *tfm)
{
	struct s5p_ace_aes_ctx *sctx = crypto_tfm_ctx(tfm);

	crypto_free_blkcipher(sctx->fallback_bc);
	sctx->fallback_bc = NULL;

#ifdef CONFIG_ACE_BC_ASYNC
	crypto_free_ablkcipher(sctx->fallback_abc);
	sctx->fallback_abc = NULL;
#endif

	S5P_ACE_DEBUG("%s\n", __func__);
}

static struct crypto_alg algs_bc[] = {
	{
		.cra_name		= "ecb(aes)",
		.cra_driver_name	= "ecb-aes-s5p-ace",
		.cra_priority		= 300,
#ifdef CONFIG_ACE_BC_ASYNC
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER
					 | CRYPTO_ALG_ASYNC,
#else
		.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
#endif
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct s5p_ace_aes_ctx),
		.cra_alignmask		= 0,
#ifdef CONFIG_ACE_BC_ASYNC
		.cra_type		= &crypto_ablkcipher_type,
#else
		.cra_type		= &crypto_blkcipher_type,
#endif
		.cra_module		= THIS_MODULE,
		.cra_init		= s5p_ace_cra_init_tfm,
		.cra_exit		= s5p_ace_cra_exit_tfm,
#ifdef CONFIG_ACE_BC_ASYNC
		.cra_ablkcipher = {
#else
		.cra_blkcipher = {
#endif
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= s5p_ace_ecb_aes_set_key,
			.encrypt	= s5p_ace_ecb_aes_encrypt,
			.decrypt	= s5p_ace_ecb_aes_decrypt,
		}
	},
	{
		.cra_name		= "cbc(aes)",
		.cra_driver_name	= "cbc-aes-s5p-ace",
		.cra_priority		= 300,
#ifdef CONFIG_ACE_BC_ASYNC
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER
					 | CRYPTO_ALG_ASYNC,
#else
		.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
#endif
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct s5p_ace_aes_ctx),
		.cra_alignmask		= 0,
#ifdef CONFIG_ACE_BC_ASYNC
		.cra_type		= &crypto_ablkcipher_type,
#else
		.cra_type		= &crypto_blkcipher_type,
#endif
		.cra_module		= THIS_MODULE,
		.cra_init		= s5p_ace_cra_init_tfm,
		.cra_exit		= s5p_ace_cra_exit_tfm,
#ifdef CONFIG_ACE_BC_ASYNC
		.cra_ablkcipher = {
#else
		.cra_blkcipher = {
#endif
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= s5p_ace_cbc_aes_set_key,
			.encrypt	= s5p_ace_cbc_aes_encrypt,
			.decrypt	= s5p_ace_cbc_aes_decrypt,
		}
	},
	{
		.cra_name		= "ctr(aes)",
		.cra_driver_name	= "ctr-aes-s5p-ace",
		.cra_priority		= 300,
#ifdef CONFIG_ACE_BC_ASYNC
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER
					 | CRYPTO_ALG_ASYNC,
#else
		.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
#endif
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct s5p_ace_aes_ctx),
		.cra_alignmask		= 0,
#ifdef CONFIG_ACE_BC_ASYNC
		.cra_type		= &crypto_ablkcipher_type,
#else
		.cra_type		= &crypto_blkcipher_type,
#endif
		.cra_module		= THIS_MODULE,
		.cra_init		= s5p_ace_cra_init_tfm,
		.cra_exit		= s5p_ace_cra_exit_tfm,
#ifdef CONFIG_ACE_BC_ASYNC
		.cra_ablkcipher = {
#else
		.cra_blkcipher = {
#endif
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= s5p_ace_ctr_aes_set_key,
			.encrypt	= s5p_ace_ctr_aes_encrypt,
			.decrypt	= s5p_ace_ctr_aes_decrypt,
		}
	}
};

#define TYPE_HASH_SHA1			0
#define TYPE_HASH_SHA256		1

#if defined(CONFIG_ACE_HASH_SHA1) || defined(CONFIG_ACE_HASH_SHA256)
struct s5p_ace_hash_ctx {
	u32		type;
	u32		prelen_high;
	u32		prelen_low;

	u32		buflen;
	u8		buffer[SHA256_BLOCK_SIZE];

	u32		state[SHA256_DIGEST_SIZE / 4];

	u32		sw_init;

	struct shash_desc		sw_desc;
	struct sha256_state		dummy;
};

/*
 *	out == NULL - This is not a final message block.
 *		Intermediate value is stored at pCtx->digest.
 *	out != NULL - This is a final message block.
 *		Digest value will be stored at out.
 */
static int s5p_ace_sha_engine(struct s5p_ace_hash_ctx *sctx,
				u8 *out, const u8* in, u32 len)
{
	u32 reg;
	u32 *buffer;
	u32 block_size, digest_size;
	u8 *in_phys;
	int transformmode = 0;

	S5P_ACE_DEBUG("Out: 0x%08X, In: 0x%08X, Len: %d\n",
			(u32)out, (u32)in, len);
	S5P_ACE_DEBUG("PreLen_Hi: %u, PreLen_Lo: %u\n",
			sctx->prelen_high, sctx->prelen_low);

	block_size = (sctx->type == TYPE_HASH_SHA1) ?
				SHA1_BLOCK_SIZE : SHA256_BLOCK_SIZE;
	digest_size = (sctx->type == TYPE_HASH_SHA1) ?
				SHA1_DIGEST_SIZE : SHA256_DIGEST_SIZE;

	if (out == NULL) {
		if (len == 0) {
			return 0;
		} else if (len < digest_size) {
			printk(KERN_ERR "%s: Invalid input\n", __func__);
			return -EINVAL;
		}
		transformmode = 1;
	}

	if (len == 0) {
		S5P_ACE_DEBUG("%s: Workaround for empty input\n", __func__);

		memset(sctx->buffer, 0, block_size - 8);
		sctx->buffer[0] = 0x80;
		reg = cpu_to_be32(sctx->prelen_high);
		memcpy(sctx->buffer + block_size - 8, &reg, 4);
		reg = cpu_to_be32(sctx->prelen_low);
		memcpy(sctx->buffer + block_size - 4, &reg, 4);

		in = sctx->buffer;
		len = block_size;
		transformmode = 1;
	}

	if ((void *)in < high_memory) {
		in_phys = (u8 *)virt_to_phys((void*)in);
	} else {
		struct page *page;
		S5P_ACE_DEBUG("%s: high memory - 0x%08x\n", __func__, (u32)in);
		page = vmalloc_to_page(in);
		if (!page)
			printk(KERN_ERR "ERROR: %s: Null page\n", __func__);
		in_phys = (u8 *)page_to_phys(page);
		in_phys += ((u32)in & ~PAGE_MASK);
	}

	/* Flush HRDMA */
	s5p_ace_write_sfr(ACE_FC_HRDMAC, ACE_FC_HRDMACFLUSH_ON);
	reg = ACE_FC_HRDMACFLUSH_OFF;
	if (s5p_ace_dev.cputype == TYPE_S5PV210)
		reg |= ACE_FC_HRDMACSWAP_ON;

#ifdef ACE_USE_ACP
	reg |= ACE_ARCACHE << ACE_FC_HRDMACARCACHE_OFS;
#endif
	s5p_ace_write_sfr(ACE_FC_HRDMAC, reg);

	/* Set byte swap of data in */
	if (s5p_ace_dev.cputype == TYPE_EXYNOS4)
		s5p_ace_write_sfr(ACE_HASH_BYTESWAP, ACE_HASH_SWAPDI_ON |
				ACE_HASH_SWAPDO_ON | ACE_HASH_SWAPIV_ON);
	else
		s5p_ace_write_sfr(ACE_HASH_BYTESWAP,
				ACE_HASH_SWAPDO_ON | ACE_HASH_SWAPIV_ON);

	/* Select Hash input mux as external source */
	reg = s5p_ace_read_sfr(ACE_FC_FIFOCTRL);
	reg = (reg & ~ACE_FC_SELHASH_MASK) | ACE_FC_SELHASH_EXOUT;
	s5p_ace_write_sfr(ACE_FC_FIFOCTRL, reg);

	/* Set Hash as SHA1 or SHA256 and start Hash engine */
	reg = (sctx->type == TYPE_HASH_SHA1) ?
			ACE_HASH_ENGSEL_SHA1HASH : ACE_HASH_ENGSEL_SHA256HASH;
	reg |= ACE_HASH_STARTBIT_ON;
	if ((sctx->prelen_low | sctx->prelen_high) != 0) {
		reg |= ACE_HASH_USERIV_EN;
		buffer = (u32 *)sctx->state;
		s5p_ace_write_sfr(ACE_HASH_IV1, buffer[0]);
		s5p_ace_write_sfr(ACE_HASH_IV2, buffer[1]);
		s5p_ace_write_sfr(ACE_HASH_IV3, buffer[2]);
		s5p_ace_write_sfr(ACE_HASH_IV4, buffer[3]);
		s5p_ace_write_sfr(ACE_HASH_IV5, buffer[4]);

		if (sctx->type == TYPE_HASH_SHA256) {
			s5p_ace_write_sfr(ACE_HASH_IV6, buffer[5]);
			s5p_ace_write_sfr(ACE_HASH_IV7, buffer[6]);
			s5p_ace_write_sfr(ACE_HASH_IV8, buffer[7]);
		}
	}
	s5p_ace_write_sfr(ACE_HASH_CONTROL, reg);

	/* Enable FIFO mode */
	s5p_ace_write_sfr(ACE_HASH_FIFO_MODE, ACE_HASH_FIFO_ON);

	/* Clean data cache */
#ifndef ACE_USE_ACP
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
	dmac_clean_range((void *)in, (void *)in + len);
#else
	dmac_map_area((void *)in, len, DMA_TO_DEVICE);
	outer_clean_range((unsigned long)in_phys, (unsigned long)in_phys + len);
#endif
#endif

	if (transformmode) {
		/* Set message length */
		s5p_ace_write_sfr(ACE_HASH_MSGSIZE_LOW, 0);
		s5p_ace_write_sfr(ACE_HASH_MSGSIZE_HIGH, 0x80000000);

		/* Set pre-message length */
		s5p_ace_write_sfr(ACE_HASH_PRELEN_LOW, 0);
		s5p_ace_write_sfr(ACE_HASH_PRELEN_HIGH, 0);
	} else {
		/* Set message length */
		s5p_ace_write_sfr(ACE_HASH_MSGSIZE_LOW, len);
		s5p_ace_write_sfr(ACE_HASH_MSGSIZE_HIGH, 0);

		/* Set pre-message length */
		s5p_ace_write_sfr(ACE_HASH_PRELEN_LOW, sctx->prelen_low);
		s5p_ace_write_sfr(ACE_HASH_PRELEN_HIGH, sctx->prelen_high);
	}

	/* Set HRDMA */
	s5p_ace_write_sfr(ACE_FC_HRDMAS, (u32)in_phys);
	s5p_ace_write_sfr(ACE_FC_HRDMAL, len);

	while (!(s5p_ace_read_sfr(ACE_FC_INTPEND) & ACE_FC_HRDMA))
		;	/* wait */
	s5p_ace_write_sfr(ACE_FC_INTPEND, ACE_FC_HRDMA);

	/*while ((s5p_ace_read_sfr(ACE_HASH_STATUS) & ACE_HASH_BUFRDY_MASK)
			== ACE_HASH_BUFRDY_OFF); */

	if (transformmode) {
		/* Set Pause bit */
		s5p_ace_write_sfr(ACE_HASH_CONTROL2, ACE_HASH_PAUSE_ON);

		while ((s5p_ace_read_sfr(ACE_HASH_STATUS)
				& ACE_HASH_PARTIALDONE_MASK)
					== ACE_HASH_PARTIALDONE_OFF)
			;	/* wait */
		s5p_ace_write_sfr(ACE_HASH_STATUS, ACE_HASH_PARTIALDONE_ON);

		if (out == NULL) {
			/* Update chaining variables */
			buffer = (u32 *)sctx->state;

			/* Update pre-message length */
			/* Note that the unit of pre-message length is a BIT! */
			sctx->prelen_low += (len << 3);
			if (sctx->prelen_low < len)
				sctx->prelen_high++;
			sctx->prelen_high += (len >> 29);
		} else {
			/* Read hash result */
			buffer = (u32 *)out;
		}
	} else {
		while ((s5p_ace_read_sfr(ACE_HASH_STATUS)
				& ACE_HASH_MSGDONE_MASK)
					== ACE_HASH_MSGDONE_OFF)
			;	/* wait */
		s5p_ace_write_sfr(ACE_HASH_STATUS, ACE_HASH_MSGDONE_ON);

		/* Read hash result */
		buffer = (u32 *)out;
	}
	buffer[0] = s5p_ace_read_sfr(ACE_HASH_RESULT1);
	buffer[1] = s5p_ace_read_sfr(ACE_HASH_RESULT2);
	buffer[2] = s5p_ace_read_sfr(ACE_HASH_RESULT3);
	buffer[3] = s5p_ace_read_sfr(ACE_HASH_RESULT4);
	buffer[4] = s5p_ace_read_sfr(ACE_HASH_RESULT5);

	if (sctx->type == TYPE_HASH_SHA256) {
		buffer[5] = s5p_ace_read_sfr(ACE_HASH_RESULT6);
		buffer[6] = s5p_ace_read_sfr(ACE_HASH_RESULT7);
		buffer[7] = s5p_ace_read_sfr(ACE_HASH_RESULT8);
	}

	return 0;
}

#ifdef CONFIG_ACE_HASH_ASYNC
static int s5p_ace_sha1_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct s5p_ace_hash_ctx *sctx = crypto_ahash_ctx(tfm);

	sctx->prelen_high = sctx->prelen_low = 0;
	sctx->buflen = 0;

	/* To Do */

	return 0;
}

static int s5p_ace_sha1_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct s5p_ace_hash_ctx *sctx = crypto_ahash_ctx(tfm);

	/* To Do */

	return 0;
}

static int s5p_ace_sha1_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct s5p_ace_hash_ctx *sctx = crypto_ahash_ctx(tfm);

	/* To Do */

	return 0;
}

static int s5p_ace_sha1_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct s5p_ace_hash_ctx *sctx = crypto_ahash_ctx(tfm);

	/* To Do */

	return 0;
}

static int s5p_ace_sha1_digest(struct ahash_request *req)
{
	s5p_ace_sha1_init(req);
	s5p_ace_sha1_update(req);
	s5p_ace_sha1_final(req);

	return 0;
}
#else
static void sha1_export_ctx_to_sw(struct shash_desc *desc)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);
	struct sha1_state *sw_ctx = shash_desc_ctx(&sctx->sw_desc);
	int i;

	if (sctx->prelen_low == 0 && sctx->prelen_high == 0)
		crypto_shash_alg(&sw_tfm[sctx->type])
				->init(&sctx->sw_desc);
	else {
		for (i = 0; i < SHA1_DIGEST_SIZE/4; i++)
			sw_ctx->state[i] = be32_to_cpu(sctx->state[i]);
	}

	sw_ctx->count = (((u64)sctx->prelen_high << 29) |
			(sctx->prelen_low >> 3)) + sctx->buflen;

	if (sctx->buflen)
		memcpy(sw_ctx->buffer, sctx->buffer, sctx->buflen);
}

static void sha256_export_ctx_to_sw(struct shash_desc *desc)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);
	struct sha256_state *sw_ctx = shash_desc_ctx(&sctx->sw_desc);
	int i;

	if (sctx->prelen_low == 0 && sctx->prelen_high == 0)
		crypto_shash_alg(&sw_tfm[sctx->type])
				->init(&sctx->sw_desc);
	else {
		for (i = 0; i < SHA256_DIGEST_SIZE/4; i++)
			sw_ctx->state[i] = be32_to_cpu(sctx->state[i]);
	}

	sw_ctx->count = (((u64)sctx->prelen_high << 29) |
			(sctx->prelen_low >> 3)) + sctx->buflen;

	if (sctx->buflen)
		memcpy(sw_ctx->buf, sctx->buffer, sctx->buflen);
}

static void sha1_import_ctx_from_sw(struct shash_desc *desc)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);
	struct sha1_state *sw_ctx = shash_desc_ctx(&sctx->sw_desc);
	int i;

	for (i = 0; i < SHA1_DIGEST_SIZE/4; i++)
		sctx->state[i] = cpu_to_be32(sw_ctx->state[i]);

	memcpy(sctx->buffer, sw_ctx->buffer, sw_ctx->count &
		(SHA1_BLOCK_SIZE - 1));
	sctx->buflen = sw_ctx->count & (SHA1_BLOCK_SIZE - 1);

	sctx->prelen_low = (sw_ctx->count - sctx->buflen) << 3;
	sctx->prelen_high = (sw_ctx->count - sctx->buflen) >> 29;
}

static void sha256_import_ctx_from_sw(struct shash_desc *desc)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);
	struct sha256_state *sw_ctx = shash_desc_ctx(&sctx->sw_desc);
	int i;

	for (i = 0; i < SHA256_DIGEST_SIZE/4; i++)
		sctx->state[i] = cpu_to_be32(sw_ctx->state[i]);

	memcpy(sctx->buffer, sw_ctx->buf, sw_ctx->count &
		(SHA256_BLOCK_SIZE - 1));
	sctx->buflen = sw_ctx->count & (SHA256_BLOCK_SIZE - 1);

	sctx->prelen_low = (sw_ctx->count - sctx->buflen) << 3;
	sctx->prelen_high = (sw_ctx->count - sctx->buflen) >> 29;
}

static void hash_export_ctx_to_sw(struct shash_desc *desc)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);
	struct sha256_state *sw_ctx = shash_desc_ctx(&sctx->sw_desc);

	if (!sctx->sw_init) {
		sw_ctx = &sctx->dummy;
		sctx->sw_init = 1;
		if (sctx->prelen_low == 0 && sctx->prelen_high == 0 &&
			sctx->buflen == 0) {
			crypto_shash_alg(&sw_tfm[sctx->type])
					->init(&sctx->sw_desc);
			return;
		}
	}

	if (sctx->type == TYPE_HASH_SHA1)
		sha1_export_ctx_to_sw(desc);
	else
		sha256_export_ctx_to_sw(desc);
}

static void hash_import_ctx_from_sw(struct shash_desc *desc)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);

	if (sctx->type == TYPE_HASH_SHA1)
		sha1_import_ctx_from_sw(desc);
	else
		sha256_import_ctx_from_sw(desc);

}

static int sha_sw_update(struct shash_desc *desc, const u8 *data, unsigned
			int len)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);

	hash_export_ctx_to_sw(desc);
	crypto_shash_alg(&sw_tfm[sctx->type])->update(&sctx->sw_desc, data,
							len);
	hash_import_ctx_from_sw(desc);

	return 0;
}

static int sha_sw_final(struct shash_desc *desc, u8 *out)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);

	hash_export_ctx_to_sw(desc);
	crypto_shash_alg(&sw_tfm[sctx->type])->final(&sctx->sw_desc, out);
	hash_import_ctx_from_sw(desc);

	return 0;
}

static int sha_sw_finup(struct shash_desc *desc, const u8 *data, unsigned int
			len, u8 *out)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);

	hash_export_ctx_to_sw(desc);
	crypto_shash_alg(&sw_tfm[sctx->type])->update(&sctx->sw_desc, data,
							len);
	crypto_shash_alg(&sw_tfm[sctx->type])->final(&sctx->sw_desc, out);
	hash_import_ctx_from_sw(desc);

	return 0;
}

static int s5p_ace_sha1_init(struct shash_desc *desc)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);

	sctx->prelen_high = sctx->prelen_low = 0;
	sctx->buflen = 0;
	sctx->type = TYPE_HASH_SHA1;
	sctx->sw_init = 0;

	return 0;
}

static int s5p_ace_sha256_init(struct shash_desc *desc)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);

	sctx->prelen_high = sctx->prelen_low = 0;
	sctx->buflen = 0;
	sctx->type = TYPE_HASH_SHA256;
	sctx->sw_init = 0;

	return 0;
}

static int s5p_ace_sha_update(struct shash_desc *desc,
			      const u8 *data, unsigned int len)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);
	const u8 *src;
	int ret = 0;
	u32 partlen, tmplen, block_size;

	S5P_ACE_DEBUG("%s (buflen: 0x%x, len: 0x%x)\n",
			__func__, sctx->buflen, len);

	s5p_ace_resume_device(&s5p_ace_dev);
	local_bh_disable();
	while (test_and_set_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags))
		udelay(1);

	if (s5p_ace_dev.flags & BIT_MASK(FLAGS_USE_SW)) {
		clear_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags);
		local_bh_enable();
		return sha_sw_update(desc, data, len);
	}

	partlen = sctx->buflen;
	src = data;

	block_size = (sctx->type == TYPE_HASH_SHA1) ?
				SHA1_BLOCK_SIZE : SHA256_BLOCK_SIZE;
	s5p_ace_clock_gating(ACE_CLOCK_ON);

	if (partlen != 0) {
		if (partlen + len < block_size) {
			memcpy(sctx->buffer + partlen, src, len);
			sctx->buflen += len;
			goto out;
		} else {
			tmplen = block_size - partlen;
			memcpy(sctx->buffer + partlen, src, tmplen);

			ret = s5p_ace_sha_engine(sctx, NULL, sctx->buffer,
						block_size);
			if (ret)
				goto out;

			len -= tmplen;
			src += tmplen;
		}
	}

	partlen = len & (block_size - 1);
	len -= partlen;
	if (len > 0) {
		ret = s5p_ace_sha_engine(sctx, NULL, src, len);
		if (ret)
			goto out;
	}

	memcpy(sctx->buffer, src + len, partlen);
	sctx->buflen = partlen;

out:
	s5p_ace_clock_gating(ACE_CLOCK_OFF);
	clear_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags);
	local_bh_enable();

	return ret;
}

static int s5p_ace_sha_final(struct shash_desc *desc, u8 *out)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);

	S5P_ACE_DEBUG("%s (buflen: 0x%x)\n", __func__, sctx->buflen);

	s5p_ace_resume_device(&s5p_ace_dev);
	local_bh_disable();
	while (test_and_set_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags))
		udelay(1);

	if (s5p_ace_dev.flags & BIT_MASK(FLAGS_USE_SW)) {
		clear_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags);
		local_bh_enable();
		return sha_sw_final(desc, out);
	}

	s5p_ace_clock_gating(ACE_CLOCK_ON);
	s5p_ace_sha_engine(sctx, out, sctx->buffer, sctx->buflen);
	s5p_ace_clock_gating(ACE_CLOCK_OFF);

	/* Wipe context */
	memset(sctx, 0, sizeof(*sctx));
	clear_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags);
	local_bh_enable();

	return 0;
}

static int s5p_ace_sha_finup(struct shash_desc *desc, const u8 *data,
		     unsigned int len, u8 *out)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);
	const u8 *src;
	int ret = 0;
	u32 block_size;

	S5P_ACE_DEBUG("%s (buflen: 0x%x, len: 0x%x)\n",
			__func__, sctx->buflen, len);

	s5p_ace_resume_device(&s5p_ace_dev);
	local_bh_disable();
	while (test_and_set_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags))
		udelay(1);

	if (s5p_ace_dev.flags & BIT_MASK(FLAGS_USE_SW)) {
		clear_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags);
		local_bh_enable();
		return sha_sw_finup(desc, data, len, out);
	}

	src = data;
	block_size = (sctx->type == TYPE_HASH_SHA1) ?
				SHA1_BLOCK_SIZE : SHA256_BLOCK_SIZE;

	s5p_ace_clock_gating(ACE_CLOCK_ON);

	if (sctx->buflen != 0) {
		if (sctx->buflen + len <= block_size) {
			memcpy(sctx->buffer + sctx->buflen, src, len);

			len += sctx->buflen;
			src = sctx->buffer;
		} else  {
			u32 copylen = block_size - sctx->buflen;
			memcpy(sctx->buffer + sctx->buflen, src, copylen);

			ret = s5p_ace_sha_engine(sctx, NULL, sctx->buffer,
						block_size);
			if (ret)
				goto out;

			len -= copylen;
			src += copylen;
		}
	}

	ret = s5p_ace_sha_engine(sctx, out, src, len);

out:
	s5p_ace_clock_gating(ACE_CLOCK_OFF);

	/* Wipe context */
	memset(sctx, 0, sizeof(*sctx));
	clear_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags);
	local_bh_enable();

	return ret;
}

static int s5p_ace_sha1_digest(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out)
{
	int ret;

	ret = s5p_ace_sha1_init(desc);
	if (ret)
		return ret;

	return s5p_ace_sha_finup(desc, data, len, out);
}

static int s5p_ace_sha256_digest(struct shash_desc *desc, const u8 *data,
		      unsigned int len, u8 *out)
{
	int ret;

	ret = s5p_ace_sha256_init(desc);
	if (ret)
		return ret;

	return s5p_ace_sha_finup(desc, data, len, out);
}

static int s5p_ace_hash_export(struct shash_desc *desc, void *out)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);
	memcpy(out, sctx, sizeof(*sctx));
	return 0;
}

static int s5p_ace_hash_import(struct shash_desc *desc, const void *in)
{
	struct s5p_ace_hash_ctx *sctx = shash_desc_ctx(desc);
	memcpy(sctx, in, sizeof(*sctx));
	return 0;
}
#endif

static int s5p_ace_hash_cra_init(struct crypto_tfm *tfm)
{
#ifdef CONFIG_ACE_HASH_ASYNC
#endif

	S5P_ACE_DEBUG("%s\n", __func__);

	return 0;
}

static void s5p_ace_hash_cra_exit(struct crypto_tfm *tfm)
{
#ifdef CONFIG_ACE_HASH_ASYNC
#endif

	S5P_ACE_DEBUG("%s\n", __func__);
}

#ifdef CONFIG_ACE_HASH_ASYNC
static struct ahash_alg algs_hash[] = {
	{
		.init		= s5p_ace_sha1_init,
		.update		= s5p_ace_sha_update,
		.final		= s5p_ace_sha_final,
		.finup		= s5p_ace_sha_finup,
		.digest		= s5p_ace_sha1_digest,
		.halg.digestsize	= SHA1_DIGEST_SIZE,
		.halg.base	= {
			.cra_name		= "sha1",
			.cra_driver_name	= "sha1-s5p-ace",
			.cra_priority		= 200,
			.cra_flags		= CRYPTO_ALG_TYPE_AHASH
						 | CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SHA1_BLOCK_SIZE,
			.cra_ctxsize	= sizeof(struct s5p_ace_hash_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= s5p_ace_hash_cra_init,
			.cra_exit		= s5p_ace_hash_cra_exit,
		}
	}
};
#else
static struct shash_alg algs_hash[] = {
	{
		.digestsize	= SHA1_DIGEST_SIZE,
		.init		= s5p_ace_sha1_init,
		.update		= s5p_ace_sha_update,
		.final		= s5p_ace_sha_final,
		.finup		= s5p_ace_sha_finup,
		.digest		= s5p_ace_sha1_digest,
		.export		= s5p_ace_hash_export,
		.import		= s5p_ace_hash_import,
		.descsize	= sizeof(struct s5p_ace_hash_ctx),
		.statesize	= sizeof(struct s5p_ace_hash_ctx),
		.base		= {
			.cra_name		= "sha1",
			.cra_driver_name	= "sha1-s5p-ace",
			.cra_priority		= 300,
			.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
			.cra_blocksize		= SHA1_BLOCK_SIZE,
			.cra_module		= THIS_MODULE,
			.cra_init		= s5p_ace_hash_cra_init,
			.cra_exit		= s5p_ace_hash_cra_exit,
		}
	},
	{
		.digestsize	= SHA256_DIGEST_SIZE,
		.init		= s5p_ace_sha256_init,
		.update		= s5p_ace_sha_update,
		.final		= s5p_ace_sha_final,
		.finup		= s5p_ace_sha_finup,
		.digest		= s5p_ace_sha256_digest,
		.export		= s5p_ace_hash_export,
		.import		= s5p_ace_hash_import,
		.descsize	= sizeof(struct s5p_ace_hash_ctx),
		.statesize	= sizeof(struct s5p_ace_hash_ctx),
		.base		= {
			.cra_name		= "sha256",
			.cra_driver_name	= "sha256-s5p-ace",
			.cra_priority		= 300,
			.cra_flags		= CRYPTO_ALG_TYPE_SHASH,
			.cra_blocksize		= SHA256_BLOCK_SIZE,
			.cra_module		= THIS_MODULE,
			.cra_init		= s5p_ace_hash_cra_init,
			.cra_exit		= s5p_ace_hash_cra_exit,
		}
	}
};
#endif		/* CONFIG_ACE_HASH_ASYNC */
#endif		/* CONFIG_ACE_HASH_SHA1 or CONFIG_ACE_HASH_SHA256 */

#if defined(CONFIG_ACE_BC_IRQMODE) || defined(CONFIG_ACE_HASH_IRQMODE)
static irqreturn_t s5p_ace_interrupt(int irq, void *data)
{
	struct s5p_ace_device *dev = data;

	s5p_ace_write_sfr(ACE_FC_INTPEND,
		ACE_FC_BRDMA | ACE_FC_BTDMA | ACE_FC_HRDMA);

#ifdef CONFIG_ACE_BC_IRQMODE
	s5p_ace_write_sfr(ACE_FC_INTENCLR, ACE_FC_BRDMA | ACE_FC_BTDMA);

	tasklet_schedule(&dev->task_bc);
#endif

#ifdef CONFIG_ACE_HASH_IRQMODE
	s5p_ace_write_sfr(ACE_FC_INTENCLR, ACE_FC_HRDMA);
#endif

	return IRQ_HANDLED;
}
#endif

int ace_s5p_get_sync_lock(void)
{
	unsigned long timeout;
	int get_lock_bc = 0, get_lock_hash = 0;

	timeout = jiffies + msecs_to_jiffies(10);
	while (time_before(jiffies, timeout)) {
		if (!test_and_set_bit(FLAGS_BC_BUSY, &s5p_ace_dev.flags)) {
			get_lock_bc = 1;
			break;
		}
		udelay(1);
	}

	timeout = jiffies + msecs_to_jiffies(10);
	while (time_before(jiffies, timeout)) {
		if (!test_and_set_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags)) {
			get_lock_hash = 1;
			break;
		}
		udelay(1);
	}

	/* set lock flag */
	if (get_lock_bc && get_lock_hash)
		set_bit(FLAGS_USE_SW, &s5p_ace_dev.flags);

	if (get_lock_bc)
		clear_bit(FLAGS_BC_BUSY, &s5p_ace_dev.flags);
	if (get_lock_hash)
		clear_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags);

	if (!(get_lock_bc && get_lock_hash))
		return -EBUSY;

	s5p_ace_clock_gating(ACE_CLOCK_ON);

	return 0;
}

int ace_s5p_release_sync_lock(void)
{
	/* clear lock flag */
	if (!test_and_clear_bit(FLAGS_USE_SW, &s5p_ace_dev.flags))
		return -ENOLCK;

	clear_bit(FLAGS_USE_SW, &s5p_ace_dev.flags);
	s5p_ace_clock_gating(ACE_CLOCK_OFF);

	return 0;
}

static int __init s5p_ace_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct s5p_ace_device *s5p_adt = &s5p_ace_dev;
	int i, j, k, m;
	int ret;

#if defined(ACE_DEBUG_HEARTBEAT) || defined(ACE_DEBUG_WATCHDOG)
	do_gettimeofday(&timestamp_base);
	for (i = 0; i < 5; i++)
		do_gettimeofday(&timestamp[i]);
#endif

	memset(s5p_adt, 0, sizeof(*s5p_adt));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get platform resource\n");
		return -ENOENT;
	}

	s5p_adt->ace_base = ioremap(res->start, resource_size(res));
	if (s5p_adt->ace_base == NULL) {
		dev_err(&pdev->dev, "failed to remap register block\n");
		ret = -ENOMEM;
		goto err_mem1;
	}

	s5p_adt->clock = clk_get(&pdev->dev, "secss");
	if (IS_ERR(s5p_adt->clock)) {
		dev_err(&pdev->dev, "failed to find clock source\n");
		ret = -EBUSY;
		goto err_clk;
	}
	s5p_ace_init_clock_gating();
	s5p_adt->cputype = platform_get_device_id(pdev)->driver_data;

#if defined(CONFIG_ACE_BC_IRQMODE) || defined(CONFIG_ACE_HASH_IRQMODE)
	s5p_adt->irq = platform_get_irq(pdev, 0);
	if (s5p_adt->irq < 0) {
		dev_err(&pdev->dev, "Failed to get irq#\n");
		s5p_adt->irq = 0;
		ret = -ENODEV;
		goto err_irq;
	}
	ret = request_irq(s5p_adt->irq, s5p_ace_interrupt, 0,
		S5P_ACE_DRIVER_NAME, (void *)s5p_adt);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request IRQ%d: err: %d.\n",
			s5p_adt->irq, ret);
		s5p_adt->irq = 0;
		ret = -ENODEV;
		goto err_irq;
	}
#endif

#ifdef ACE_USE_ACP
	s5p_adt->sss_usercon = ioremap(PA_SSS_USER_CON & PAGE_MASK, SZ_4K);
	if (s5p_adt->sss_usercon == NULL) {
		dev_err(&pdev->dev, "failed to remap register SSS_USER_CON\n");
		ret = -EBUSY;
		goto err_mem2;
	}

	/* Set ARUSER[12:8] and AWUSER[4:0] */
	writel(0x101, s5p_adt->sss_usercon
		+ (PA_SSS_USER_CON & (PAGE_SIZE - 1)));
#endif

	spin_lock_init(&s5p_adt->lock);
	s5p_adt->flags = 0;
	hrtimer_init(&s5p_adt->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	s5p_adt->timer.function = s5p_ace_timer_func;
	INIT_WORK(&s5p_adt->work, s5p_ace_deferred_clock_disable);
#ifdef ACE_DEBUG_HEARTBEAT
	hrtimer_init(&s5p_adt->heartbeat, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	s5p_adt->heartbeat.function = s5p_ace_heartbeat_func;
	hrtimer_start(&s5p_ace_dev.heartbeat,
		ns_to_ktime((u64)ACE_HEARTBEAT_MS * NSEC_PER_MSEC),
		HRTIMER_MODE_REL);
#endif
#ifdef ACE_DEBUG_WATCHDOG
	hrtimer_init(&s5p_adt->watchdog_bc, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	s5p_adt->watchdog_bc.function = s5p_ace_watchdog_bc_func;
#endif

#ifdef CONFIG_ACE_BC_ASYNC
	crypto_init_queue(&s5p_adt->queue_bc, 1);
	tasklet_init(&s5p_adt->task_bc, s5p_ace_bc_task,
			(unsigned long)s5p_adt);
#endif

#ifdef CONFIG_ACE_HASH_ASYNC
	crypto_init_queue(&s5p_adt->queue_hash, 1);
	tasklet_init(&s5p_adt->task_hash, s5p_ace_hash_task,
			(unsigned long)s5p_adt);
#endif

	for (i = 0; i < ARRAY_SIZE(algs_bc); i++) {
		INIT_LIST_HEAD(&algs_bc[i].cra_list);
		algs_bc[i].cra_flags |= CRYPTO_ALG_NEED_FALLBACK;
		ret = crypto_register_alg(&algs_bc[i]);
		if (ret)
			goto err_reg_bc;
		printk(KERN_INFO "ACE: %s\n", algs_bc[i].cra_driver_name);
	}

#if defined(CONFIG_ACE_HASH_SHA1) || defined(CONFIG_ACE_HASH_SHA256)
	fallback_hash = (struct crypto_hash **)
			kmalloc(sizeof(struct crypto_hash *) *
				ARRAY_SIZE(algs_hash), GFP_KERNEL);
	sw_tfm = (struct crypto_shash *) kmalloc(sizeof(struct crypto_shash)
						* ARRAY_SIZE(algs_hash),
						GFP_KERNEL);

	for (m = 0; m < ARRAY_SIZE(algs_hash); m++) {
		fallback_hash[m] =
				crypto_alloc_hash(algs_hash[m].base.cra_name, 0,
						 CRYPTO_ALG_ASYNC);

		if (IS_ERR(fallback_hash[m])) {
			printk(KERN_ERR "failed to load transform for %s: %ld\n",
				algs_hash[m].base.cra_name,
				PTR_ERR(fallback_hash[m]));
			goto err_fallback_hash;
		}

		sw_tfm[m].base.__crt_alg = fallback_hash[m]->base.__crt_alg;
	}

	for (j = 0; j < ARRAY_SIZE(algs_hash); j++) {
#ifdef CONFIG_ACE_HASH_ASYNC
		ret = crypto_register_ahash(&algs_hash[j]);
#else
		ret = crypto_register_shash(&algs_hash[j]);
#endif
		if (ret)
			goto err_reg_hash;
#ifdef CONFIG_ACE_HASH_ASYNC
		printk(KERN_INFO "ACE: %s\n",
			algs_hash[j].halg.base.cra_driver_name);
#else
		printk(KERN_INFO "ACE: %s\n",
			algs_hash[j].base.cra_driver_name);
#endif
	}
#endif

	secmem_ftn.lock = &ace_s5p_get_sync_lock;
	secmem_ftn.release = &ace_s5p_release_sync_lock;
	secmem_crypto_register(&secmem_ftn);

	printk(KERN_NOTICE "ACE driver is initialized\n");

	return 0;

#if defined(CONFIG_ACE_HASH_SHA1) || defined(CONFIG_ACE_HASH_SHA256)
err_reg_hash:
	for (k = 0; k < j; k++)
#ifdef CONFIG_ACE_HASH_ASYNC
		crypto_unregister_ahash(&algs_hash[k]);
#else
		crypto_unregister_shash(&algs_hash[k]);
#endif
err_fallback_hash:
	kfree(sw_tfm);
	for (k = 0; k < m; k++)
		crypto_free_hash(fallback_hash[k]);
	kfree(fallback_hash);
#endif
err_reg_bc:
	for (k = 0; k < i; k++)
		crypto_unregister_alg(&algs_bc[k]);
#ifdef CONFIG_ACE_BC_ASYNC
	tasklet_kill(&s5p_adt->task_bc);
#endif
#ifdef CONFIG_ACE_HASH_ASYNC
	tasklet_kill(&s5p_adt->task_hash);
#endif
#ifdef ACE_USE_ACP
	iounmap(s5p_adt->sss_usercon);
err_mem2:
#endif
#if defined(CONFIG_ACE_BC_IRQMODE) || defined(CONFIG_ACE_HASH_IRQMODE)
err_irq:
	free_irq(s5p_adt->irq, (void *)s5p_adt);
	s5p_adt->irq = 0;
#endif
err_clk:
	iounmap(s5p_adt->ace_base);
	s5p_adt->ace_base = NULL;
err_mem1:

	printk(KERN_ERR "ACE driver initialization failed.\n");

	return ret;
}

static int s5p_ace_remove(struct platform_device *dev)
{
	struct s5p_ace_device *s5p_adt = &s5p_ace_dev;
	int i;

#ifdef ACE_DEBUG_HEARTBEAT
	hrtimer_cancel(&s5p_adt->heartbeat);
#endif

#if defined(CONFIG_ACE_BC_IRQMODE) || defined(CONFIG_ACE_HASH_IRQMODE)
	if (s5p_adt->irq) {
		free_irq(s5p_adt->irq, (void *)s5p_adt);
		s5p_adt->irq = 0;
	}
#endif

	if (s5p_adt->clock) {
		clk_put(s5p_adt->clock);
		s5p_adt->clock = NULL;
	}

	if (s5p_adt->ace_base) {
		iounmap(s5p_adt->ace_base);
		s5p_adt->ace_base = NULL;
	}

#ifdef ACE_USE_ACP
	if (s5p_adt->sss_usercon) {
		iounmap(s5p_adt->sss_usercon);
		s5p_adt->sss_usercon = NULL;
	}
#endif

	secmem_crypto_deregister();

#if defined(CONFIG_ACE_HASH_SHA1) || defined(CONFIG_ACE_HASH_SHA256)
	kfree(sw_tfm);
	for (i = 0; i < ARRAY_SIZE(algs_hash); i++)
		crypto_free_hash(fallback_hash[i]);

	kfree(fallback_hash);

	for (i = 0; i < ARRAY_SIZE(algs_hash); i++)
#ifdef CONFIG_ACE_HASH_ASYNC
		crypto_unregister_ahash(&algs_hash[i]);
#else
		crypto_unregister_shash(&algs_hash[i]);
#endif
#endif

	for (i = 0; i < ARRAY_SIZE(algs_bc); i++)
		crypto_unregister_alg(&algs_bc[i]);

#ifdef CONFIG_ACE_BC_ASYNC
	tasklet_kill(&s5p_adt->task_bc);
#endif
#ifdef CONFIG_ACE_HASH_ASYNC
	tasklet_kill(&s5p_adt->task_hash);
#endif

	flush_work(&s5p_ace_dev.work);

	printk(KERN_INFO "ACE driver is removed\n");

	return 0;
}

static int s5p_ace_suspend(struct platform_device *dev, pm_message_t state)
{
	unsigned long timeout;
	int get_lock_bc = 0, get_lock_hash = 0;

#if defined(ACE_DEBUG_HEARTBEAT) || defined(ACE_DEBUG_WATCHDOG)
	do_gettimeofday(&timestamp[3]);		/* 3: suspend */
#endif

	timeout = jiffies + msecs_to_jiffies(10);
	while (time_before(jiffies, timeout)) {
		if (!test_and_set_bit(FLAGS_BC_BUSY, &s5p_ace_dev.flags)) {
			get_lock_bc = 1;
			break;
		}
		udelay(1);
	}
	timeout = jiffies + msecs_to_jiffies(10);
	while (time_before(jiffies, timeout)) {
		if (!test_and_set_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags)) {
			get_lock_hash = 1;
			break;
		}
		udelay(1);
	}

	if (get_lock_bc && get_lock_hash) {
		set_bit(FLAGS_SUSPENDED, &s5p_ace_dev.flags);
		return 0;
	}

	printk(KERN_ERR "ACE: suspend: time out.\n");

	if (get_lock_bc)
		clear_bit(FLAGS_BC_BUSY, &s5p_ace_dev.flags);
	if (get_lock_hash)
		clear_bit(FLAGS_HASH_BUSY, &s5p_ace_dev.flags);

	return -EBUSY;
}

static int s5p_ace_resume(struct platform_device *dev)
{
#if defined(ACE_DEBUG_HEARTBEAT) || defined(ACE_DEBUG_WATCHDOG)
	do_gettimeofday(&timestamp[4]);		/* 4: resume */
#endif

	s5p_ace_resume_device(&s5p_ace_dev);

	return 0;
}

static struct platform_device_id s5p_ace_driver_ids[] = {
	{
		.name		= "s5pv210-ace",
		.driver_data	= TYPE_S5PV210,
	}, {
		.name		= "exynos4-ace",
		.driver_data	= TYPE_EXYNOS4,
	},
	{}
};
MODULE_DEVICE_TABLE(platform, s5p_ace_driver_ids);

static struct platform_driver s5p_ace_driver = {
	.probe		= s5p_ace_probe,
	.remove		= s5p_ace_remove,
	.suspend	= s5p_ace_suspend,
	.resume		= s5p_ace_resume,
	.id_table	= s5p_ace_driver_ids,
	.driver		= {
		.name	= S5P_ACE_DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init s5p_ace_init(void)
{
	printk(KERN_INFO "S5P ACE Driver, (c) 2010 Samsung Electronics\n");

	return platform_driver_register(&s5p_ace_driver);
}

static void __exit s5p_ace_exit(void)
{
	platform_driver_unregister(&s5p_ace_driver);
}

module_init(s5p_ace_init);
module_exit(s5p_ace_exit);

MODULE_DESCRIPTION("S5P ACE(Advanced Crypto Engine) support");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Dong Jin PARK");

