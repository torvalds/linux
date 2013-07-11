/*
 * dma_compat.h helper code for mixing sun4i/sun5i and sun7i dma code
 *
 * Copyright 2013 Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#ifndef __PLAT_DMA_COMPAT_H__
#define __PLAT_DMA_COMPAT_H__

#include <mach/dma.h>

#ifdef CONFIG_ARCH_SUN7I
struct sw_dma_client {
	char *name;
};
#endif

struct sunxi_dma_params {
	struct sw_dma_client client;
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	unsigned int channel;	/* Channel ID */
#else
	dma_hdl_t  dma_hdl;
	dma_cb_t   dma_cb;
#endif	
	dma_addr_t dma_addr;
	void (*callback)(struct sunxi_dma_params *, void *);
	void *callback_arg;
};

#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
static void sunxi_dma_callback(struct sw_dma_chan *channel, void *dev_id,
	int size, enum sw_dma_buffresult result)
{
	struct sunxi_dma_params *dma = dev_id;

	if (result == SW_RES_ABORT || result == SW_RES_ERR)
		return;

	dma->callback(dma, dma->callback_arg);
}
#else
static void sunxi_dma_callback(dma_hdl_t dma_hdl, void *parg)
{
	struct sunxi_dma_params *dma = parg;

	dma->callback(dma, dma->callback_arg);
}
#endif

static inline int sunxi_dma_set_callback(struct sunxi_dma_params *dma,
	void (*callback)(struct sunxi_dma_params *, void *),
	void *callback_arg)
{
	dma->callback = callback;
	dma->callback_arg = callback_arg;
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	sw_dma_set_buffdone_fn(dma->channel, sunxi_dma_callback);
	return 0;
#else
	dma->dma_cb.func = sunxi_dma_callback;
	dma->dma_cb.parg = dma;
	/* use the full buffer cb, maybe we should use the half buffer cb? */
	return sw_dma_ctl(dma->dma_hdl, DMA_OP_SET_FD_CB,
			  (void *)&dma->dma_cb);
#endif
}

static inline int sunxi_dma_enqueue(struct sunxi_dma_params *dma,
	dma_addr_t pos, unsigned long len, int read)
{
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	return sw_dma_enqueue(dma->channel, dma, __bus_to_virt(pos),  len);
#else
	if (read)
		return sw_dma_enqueue(dma->dma_hdl, dma->dma_addr, pos, len);
	else
		return sw_dma_enqueue(dma->dma_hdl, pos, dma->dma_addr, len);
#endif
}

static inline int sunxi_dma_config(struct sunxi_dma_params *dma,
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	struct dma_hw_conf *config,
#else
	dma_config_t *config,
#endif
	unsigned int cmbk)
{
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	config->cmbk = cmbk;
	return sw_dma_config(dma->channel, config);
#else
	int ret = sw_dma_config(dma->dma_hdl, config);
	if (ret || cmbk == 0)
		return ret;
	return sw_dma_ctl(dma->dma_hdl, DMA_OP_SET_PARA_REG, &cmbk);
#endif
}


static inline int sunxi_dma_start(struct sunxi_dma_params *dma)
{
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	return sw_dma_ctrl(dma->channel, SW_DMAOP_START);
#else
	return sw_dma_ctl(dma->dma_hdl, DMA_OP_START, NULL);
#endif
}

static inline int sunxi_dma_stop(struct sunxi_dma_params *dma)
{
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	return sw_dma_ctrl(dma->channel, SW_DMAOP_STOP);
#else
	return sw_dma_ctl(dma->dma_hdl, DMA_OP_STOP, NULL);
#endif
}

static inline int sunxi_dma_flush(struct sunxi_dma_params *dma)
{
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	sw_dma_ctrl(dma->channel, SW_DMAOP_FLUSH);
	return 0;
#else
	return -EINVAL; /* No flush in the sun7i dma code */
#endif
}

static inline void sunxi_dma_started(struct sunxi_dma_params *dma)
{
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	sw_dma_ctrl(dma->channel, SW_DMAOP_STARTED);
#endif
}

static inline int sunxi_dma_getcurposition(struct sunxi_dma_params *dma,
	dma_addr_t *src, dma_addr_t *dest)
{
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	return sw_dma_getcurposition(dma->channel, src, dest);
#else
	return sw_dma_getposition(dma->dma_hdl, src, dest);
#endif
}

static inline int sunxi_dma_request(struct sunxi_dma_params *dma,
	int dedicated)
{
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	return sw_dma_request(dma->channel, &dma->client, NULL);
#else
	dma->dma_hdl = sw_dma_request(dma->client.name,
				dedicated ? CHAN_DEDICATE : CHAN_NORMAL);
	return (dma->dma_hdl != NULL) ? 0 : -EIO;
#endif
}

static inline void sunxi_dma_release(struct sunxi_dma_params *dma)
{
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	sw_dma_free(dma->channel, &dma->client);
#else
	sw_dma_release(dma->dma_hdl);
	dma->dma_hdl = NULL;
#endif
}

#endif
