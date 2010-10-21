/*
 * Copyright 2005 Stephane Marchesin
 * Copyright 2008 Stuart Bennett
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/swab.h>
#include <linux/slab.h>
#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"
#include "drm_crtc_helper.h"
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>

#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nouveau_fbcon.h"
#include "nouveau_ramht.h"
#include "nouveau_pm.h"
#include "nv50_display.h"

static void nouveau_stub_takedown(struct drm_device *dev) {}
static int nouveau_stub_init(struct drm_device *dev) { return 0; }

static int nouveau_init_engine_ptrs(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;

	switch (dev_priv->chipset & 0xf0) {
	case 0x00:
		engine->instmem.init		= nv04_instmem_init;
		engine->instmem.takedown	= nv04_instmem_takedown;
		engine->instmem.suspend		= nv04_instmem_suspend;
		engine->instmem.resume		= nv04_instmem_resume;
		engine->instmem.populate	= nv04_instmem_populate;
		engine->instmem.clear		= nv04_instmem_clear;
		engine->instmem.bind		= nv04_instmem_bind;
		engine->instmem.unbind		= nv04_instmem_unbind;
		engine->instmem.flush		= nv04_instmem_flush;
		engine->mc.init			= nv04_mc_init;
		engine->mc.takedown		= nv04_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv04_fb_init;
		engine->fb.takedown		= nv04_fb_takedown;
		engine->graph.init		= nv04_graph_init;
		engine->graph.takedown		= nv04_graph_takedown;
		engine->graph.fifo_access	= nv04_graph_fifo_access;
		engine->graph.channel		= nv04_graph_channel;
		engine->graph.create_context	= nv04_graph_create_context;
		engine->graph.destroy_context	= nv04_graph_destroy_context;
		engine->graph.load_context	= nv04_graph_load_context;
		engine->graph.unload_context	= nv04_graph_unload_context;
		engine->fifo.channels		= 16;
		engine->fifo.init		= nv04_fifo_init;
		engine->fifo.takedown		= nouveau_stub_takedown;
		engine->fifo.disable		= nv04_fifo_disable;
		engine->fifo.enable		= nv04_fifo_enable;
		engine->fifo.reassign		= nv04_fifo_reassign;
		engine->fifo.cache_pull		= nv04_fifo_cache_pull;
		engine->fifo.channel_id		= nv04_fifo_channel_id;
		engine->fifo.create_context	= nv04_fifo_create_context;
		engine->fifo.destroy_context	= nv04_fifo_destroy_context;
		engine->fifo.load_context	= nv04_fifo_load_context;
		engine->fifo.unload_context	= nv04_fifo_unload_context;
		engine->display.early_init	= nv04_display_early_init;
		engine->display.late_takedown	= nv04_display_late_takedown;
		engine->display.create		= nv04_display_create;
		engine->display.init		= nv04_display_init;
		engine->display.destroy		= nv04_display_destroy;
		engine->gpio.init		= nouveau_stub_init;
		engine->gpio.takedown		= nouveau_stub_takedown;
		engine->gpio.get		= NULL;
		engine->gpio.set		= NULL;
		engine->gpio.irq_enable		= NULL;
		engine->pm.clock_get		= nv04_pm_clock_get;
		engine->pm.clock_pre		= nv04_pm_clock_pre;
		engine->pm.clock_set		= nv04_pm_clock_set;
		engine->crypt.init		= nouveau_stub_init;
		engine->crypt.takedown		= nouveau_stub_takedown;
		break;
	case 0x10:
		engine->instmem.init		= nv04_instmem_init;
		engine->instmem.takedown	= nv04_instmem_takedown;
		engine->instmem.suspend		= nv04_instmem_suspend;
		engine->instmem.resume		= nv04_instmem_resume;
		engine->instmem.populate	= nv04_instmem_populate;
		engine->instmem.clear		= nv04_instmem_clear;
		engine->instmem.bind		= nv04_instmem_bind;
		engine->instmem.unbind		= nv04_instmem_unbind;
		engine->instmem.flush		= nv04_instmem_flush;
		engine->mc.init			= nv04_mc_init;
		engine->mc.takedown		= nv04_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv10_fb_init;
		engine->fb.takedown		= nv10_fb_takedown;
		engine->fb.set_region_tiling	= nv10_fb_set_region_tiling;
		engine->graph.init		= nv10_graph_init;
		engine->graph.takedown		= nv10_graph_takedown;
		engine->graph.channel		= nv10_graph_channel;
		engine->graph.create_context	= nv10_graph_create_context;
		engine->graph.destroy_context	= nv10_graph_destroy_context;
		engine->graph.fifo_access	= nv04_graph_fifo_access;
		engine->graph.load_context	= nv10_graph_load_context;
		engine->graph.unload_context	= nv10_graph_unload_context;
		engine->graph.set_region_tiling	= nv10_graph_set_region_tiling;
		engine->fifo.channels		= 32;
		engine->fifo.init		= nv10_fifo_init;
		engine->fifo.takedown		= nouveau_stub_takedown;
		engine->fifo.disable		= nv04_fifo_disable;
		engine->fifo.enable		= nv04_fifo_enable;
		engine->fifo.reassign		= nv04_fifo_reassign;
		engine->fifo.cache_pull		= nv04_fifo_cache_pull;
		engine->fifo.channel_id		= nv10_fifo_channel_id;
		engine->fifo.create_context	= nv10_fifo_create_context;
		engine->fifo.destroy_context	= nv04_fifo_destroy_context;
		engine->fifo.load_context	= nv10_fifo_load_context;
		engine->fifo.unload_context	= nv10_fifo_unload_context;
		engine->display.early_init	= nv04_display_early_init;
		engine->display.late_takedown	= nv04_display_late_takedown;
		engine->display.create		= nv04_display_create;
		engine->display.init		= nv04_display_init;
		engine->display.destroy		= nv04_display_destroy;
		engine->gpio.init		= nouveau_stub_init;
		engine->gpio.takedown		= nouveau_stub_takedown;
		engine->gpio.get		= nv10_gpio_get;
		engine->gpio.set		= nv10_gpio_set;
		engine->gpio.irq_enable		= NULL;
		engine->pm.clock_get		= nv04_pm_clock_get;
		engine->pm.clock_pre		= nv04_pm_clock_pre;
		engine->pm.clock_set		= nv04_pm_clock_set;
		engine->crypt.init		= nouveau_stub_init;
		engine->crypt.takedown		= nouveau_stub_takedown;
		break;
	case 0x20:
		engine->instmem.init		= nv04_instmem_init;
		engine->instmem.takedown	= nv04_instmem_takedown;
		engine->instmem.suspend		= nv04_instmem_suspend;
		engine->instmem.resume		= nv04_instmem_resume;
		engine->instmem.populate	= nv04_instmem_populate;
		engine->instmem.clear		= nv04_instmem_clear;
		engine->instmem.bind		= nv04_instmem_bind;
		engine->instmem.unbind		= nv04_instmem_unbind;
		engine->instmem.flush		= nv04_instmem_flush;
		engine->mc.init			= nv04_mc_init;
		engine->mc.takedown		= nv04_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv10_fb_init;
		engine->fb.takedown		= nv10_fb_takedown;
		engine->fb.set_region_tiling	= nv10_fb_set_region_tiling;
		engine->graph.init		= nv20_graph_init;
		engine->graph.takedown		= nv20_graph_takedown;
		engine->graph.channel		= nv10_graph_channel;
		engine->graph.create_context	= nv20_graph_create_context;
		engine->graph.destroy_context	= nv20_graph_destroy_context;
		engine->graph.fifo_access	= nv04_graph_fifo_access;
		engine->graph.load_context	= nv20_graph_load_context;
		engine->graph.unload_context	= nv20_graph_unload_context;
		engine->graph.set_region_tiling	= nv20_graph_set_region_tiling;
		engine->fifo.channels		= 32;
		engine->fifo.init		= nv10_fifo_init;
		engine->fifo.takedown		= nouveau_stub_takedown;
		engine->fifo.disable		= nv04_fifo_disable;
		engine->fifo.enable		= nv04_fifo_enable;
		engine->fifo.reassign		= nv04_fifo_reassign;
		engine->fifo.cache_pull		= nv04_fifo_cache_pull;
		engine->fifo.channel_id		= nv10_fifo_channel_id;
		engine->fifo.create_context	= nv10_fifo_create_context;
		engine->fifo.destroy_context	= nv04_fifo_destroy_context;
		engine->fifo.load_context	= nv10_fifo_load_context;
		engine->fifo.unload_context	= nv10_fifo_unload_context;
		engine->display.early_init	= nv04_display_early_init;
		engine->display.late_takedown	= nv04_display_late_takedown;
		engine->display.create		= nv04_display_create;
		engine->display.init		= nv04_display_init;
		engine->display.destroy		= nv04_display_destroy;
		engine->gpio.init		= nouveau_stub_init;
		engine->gpio.takedown		= nouveau_stub_takedown;
		engine->gpio.get		= nv10_gpio_get;
		engine->gpio.set		= nv10_gpio_set;
		engine->gpio.irq_enable		= NULL;
		engine->pm.clock_get		= nv04_pm_clock_get;
		engine->pm.clock_pre		= nv04_pm_clock_pre;
		engine->pm.clock_set		= nv04_pm_clock_set;
		engine->crypt.init		= nouveau_stub_init;
		engine->crypt.takedown		= nouveau_stub_takedown;
		break;
	case 0x30:
		engine->instmem.init		= nv04_instmem_init;
		engine->instmem.takedown	= nv04_instmem_takedown;
		engine->instmem.suspend		= nv04_instmem_suspend;
		engine->instmem.resume		= nv04_instmem_resume;
		engine->instmem.populate	= nv04_instmem_populate;
		engine->instmem.clear		= nv04_instmem_clear;
		engine->instmem.bind		= nv04_instmem_bind;
		engine->instmem.unbind		= nv04_instmem_unbind;
		engine->instmem.flush		= nv04_instmem_flush;
		engine->mc.init			= nv04_mc_init;
		engine->mc.takedown		= nv04_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv30_fb_init;
		engine->fb.takedown		= nv30_fb_takedown;
		engine->fb.set_region_tiling	= nv10_fb_set_region_tiling;
		engine->graph.init		= nv30_graph_init;
		engine->graph.takedown		= nv20_graph_takedown;
		engine->graph.fifo_access	= nv04_graph_fifo_access;
		engine->graph.channel		= nv10_graph_channel;
		engine->graph.create_context	= nv20_graph_create_context;
		engine->graph.destroy_context	= nv20_graph_destroy_context;
		engine->graph.load_context	= nv20_graph_load_context;
		engine->graph.unload_context	= nv20_graph_unload_context;
		engine->graph.set_region_tiling	= nv20_graph_set_region_tiling;
		engine->fifo.channels		= 32;
		engine->fifo.init		= nv10_fifo_init;
		engine->fifo.takedown		= nouveau_stub_takedown;
		engine->fifo.disable		= nv04_fifo_disable;
		engine->fifo.enable		= nv04_fifo_enable;
		engine->fifo.reassign		= nv04_fifo_reassign;
		engine->fifo.cache_pull		= nv04_fifo_cache_pull;
		engine->fifo.channel_id		= nv10_fifo_channel_id;
		engine->fifo.create_context	= nv10_fifo_create_context;
		engine->fifo.destroy_context	= nv04_fifo_destroy_context;
		engine->fifo.load_context	= nv10_fifo_load_context;
		engine->fifo.unload_context	= nv10_fifo_unload_context;
		engine->display.early_init	= nv04_display_early_init;
		engine->display.late_takedown	= nv04_display_late_takedown;
		engine->display.create		= nv04_display_create;
		engine->display.init		= nv04_display_init;
		engine->display.destroy		= nv04_display_destroy;
		engine->gpio.init		= nouveau_stub_init;
		engine->gpio.takedown		= nouveau_stub_takedown;
		engine->gpio.get		= nv10_gpio_get;
		engine->gpio.set		= nv10_gpio_set;
		engine->gpio.irq_enable		= NULL;
		engine->pm.clock_get		= nv04_pm_clock_get;
		engine->pm.clock_pre		= nv04_pm_clock_pre;
		engine->pm.clock_set		= nv04_pm_clock_set;
		engine->pm.voltage_get		= nouveau_voltage_gpio_get;
		engine->pm.voltage_set		= nouveau_voltage_gpio_set;
		engine->crypt.init		= nouveau_stub_init;
		engine->crypt.takedown		= nouveau_stub_takedown;
		break;
	case 0x40:
	case 0x60:
		engine->instmem.init		= nv04_instmem_init;
		engine->instmem.takedown	= nv04_instmem_takedown;
		engine->instmem.suspend		= nv04_instmem_suspend;
		engine->instmem.resume		= nv04_instmem_resume;
		engine->instmem.populate	= nv04_instmem_populate;
		engine->instmem.clear		= nv04_instmem_clear;
		engine->instmem.bind		= nv04_instmem_bind;
		engine->instmem.unbind		= nv04_instmem_unbind;
		engine->instmem.flush		= nv04_instmem_flush;
		engine->mc.init			= nv40_mc_init;
		engine->mc.takedown		= nv40_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv40_fb_init;
		engine->fb.takedown		= nv40_fb_takedown;
		engine->fb.set_region_tiling	= nv40_fb_set_region_tiling;
		engine->graph.init		= nv40_graph_init;
		engine->graph.takedown		= nv40_graph_takedown;
		engine->graph.fifo_access	= nv04_graph_fifo_access;
		engine->graph.channel		= nv40_graph_channel;
		engine->graph.create_context	= nv40_graph_create_context;
		engine->graph.destroy_context	= nv40_graph_destroy_context;
		engine->graph.load_context	= nv40_graph_load_context;
		engine->graph.unload_context	= nv40_graph_unload_context;
		engine->graph.set_region_tiling	= nv40_graph_set_region_tiling;
		engine->fifo.channels		= 32;
		engine->fifo.init		= nv40_fifo_init;
		engine->fifo.takedown		= nouveau_stub_takedown;
		engine->fifo.disable		= nv04_fifo_disable;
		engine->fifo.enable		= nv04_fifo_enable;
		engine->fifo.reassign		= nv04_fifo_reassign;
		engine->fifo.cache_pull		= nv04_fifo_cache_pull;
		engine->fifo.channel_id		= nv10_fifo_channel_id;
		engine->fifo.create_context	= nv40_fifo_create_context;
		engine->fifo.destroy_context	= nv04_fifo_destroy_context;
		engine->fifo.load_context	= nv40_fifo_load_context;
		engine->fifo.unload_context	= nv40_fifo_unload_context;
		engine->display.early_init	= nv04_display_early_init;
		engine->display.late_takedown	= nv04_display_late_takedown;
		engine->display.create		= nv04_display_create;
		engine->display.init		= nv04_display_init;
		engine->display.destroy		= nv04_display_destroy;
		engine->gpio.init		= nouveau_stub_init;
		engine->gpio.takedown		= nouveau_stub_takedown;
		engine->gpio.get		= nv10_gpio_get;
		engine->gpio.set		= nv10_gpio_set;
		engine->gpio.irq_enable		= NULL;
		engine->pm.clock_get		= nv04_pm_clock_get;
		engine->pm.clock_pre		= nv04_pm_clock_pre;
		engine->pm.clock_set		= nv04_pm_clock_set;
		engine->pm.voltage_get		= nouveau_voltage_gpio_get;
		engine->pm.voltage_set		= nouveau_voltage_gpio_set;
		engine->pm.temp_get		= nv40_temp_get;
		engine->crypt.init		= nouveau_stub_init;
		engine->crypt.takedown		= nouveau_stub_takedown;
		break;
	case 0x50:
	case 0x80: /* gotta love NVIDIA's consistency.. */
	case 0x90:
	case 0xA0:
		engine->instmem.init		= nv50_instmem_init;
		engine->instmem.takedown	= nv50_instmem_takedown;
		engine->instmem.suspend		= nv50_instmem_suspend;
		engine->instmem.resume		= nv50_instmem_resume;
		engine->instmem.populate	= nv50_instmem_populate;
		engine->instmem.clear		= nv50_instmem_clear;
		engine->instmem.bind		= nv50_instmem_bind;
		engine->instmem.unbind		= nv50_instmem_unbind;
		if (dev_priv->chipset == 0x50)
			engine->instmem.flush	= nv50_instmem_flush;
		else
			engine->instmem.flush	= nv84_instmem_flush;
		engine->mc.init			= nv50_mc_init;
		engine->mc.takedown		= nv50_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv50_fb_init;
		engine->fb.takedown		= nv50_fb_takedown;
		engine->graph.init		= nv50_graph_init;
		engine->graph.takedown		= nv50_graph_takedown;
		engine->graph.fifo_access	= nv50_graph_fifo_access;
		engine->graph.channel		= nv50_graph_channel;
		engine->graph.create_context	= nv50_graph_create_context;
		engine->graph.destroy_context	= nv50_graph_destroy_context;
		engine->graph.load_context	= nv50_graph_load_context;
		engine->graph.unload_context	= nv50_graph_unload_context;
		if (dev_priv->chipset != 0x86)
			engine->graph.tlb_flush	= nv50_graph_tlb_flush;
		else {
			/* from what i can see nvidia do this on every
			 * pre-NVA3 board except NVAC, but, we've only
			 * ever seen problems on NV86
			 */
			engine->graph.tlb_flush	= nv86_graph_tlb_flush;
		}
		engine->fifo.channels		= 128;
		engine->fifo.init		= nv50_fifo_init;
		engine->fifo.takedown		= nv50_fifo_takedown;
		engine->fifo.disable		= nv04_fifo_disable;
		engine->fifo.enable		= nv04_fifo_enable;
		engine->fifo.reassign		= nv04_fifo_reassign;
		engine->fifo.channel_id		= nv50_fifo_channel_id;
		engine->fifo.create_context	= nv50_fifo_create_context;
		engine->fifo.destroy_context	= nv50_fifo_destroy_context;
		engine->fifo.load_context	= nv50_fifo_load_context;
		engine->fifo.unload_context	= nv50_fifo_unload_context;
		engine->fifo.tlb_flush		= nv50_fifo_tlb_flush;
		engine->display.early_init	= nv50_display_early_init;
		engine->display.late_takedown	= nv50_display_late_takedown;
		engine->display.create		= nv50_display_create;
		engine->display.init		= nv50_display_init;
		engine->display.destroy		= nv50_display_destroy;
		engine->gpio.init		= nv50_gpio_init;
		engine->gpio.takedown		= nouveau_stub_takedown;
		engine->gpio.get		= nv50_gpio_get;
		engine->gpio.set		= nv50_gpio_set;
		engine->gpio.irq_enable		= nv50_gpio_irq_enable;
		switch (dev_priv->chipset) {
		case 0x84:
		case 0x86:
		case 0x92:
		case 0x94:
		case 0x96:
		case 0x98:
		case 0xa0:
		case 0xaa:
		case 0xac:
		case 0x50:
			engine->pm.clock_get	= nv50_pm_clock_get;
			engine->pm.clock_pre	= nv50_pm_clock_pre;
			engine->pm.clock_set	= nv50_pm_clock_set;
			break;
		default:
			engine->pm.clock_get	= nva3_pm_clock_get;
			engine->pm.clock_pre	= nva3_pm_clock_pre;
			engine->pm.clock_set	= nva3_pm_clock_set;
			break;
		}
		engine->pm.voltage_get		= nouveau_voltage_gpio_get;
		engine->pm.voltage_set		= nouveau_voltage_gpio_set;
		if (dev_priv->chipset >= 0x84)
			engine->pm.temp_get	= nv84_temp_get;
		else
			engine->pm.temp_get	= nv40_temp_get;
		switch (dev_priv->chipset) {
		case 0x84:
		case 0x86:
		case 0x92:
		case 0x94:
		case 0x96:
		case 0xa0:
			engine->crypt.init	= nv84_crypt_init;
			engine->crypt.takedown	= nv84_crypt_fini;
			engine->crypt.create_context = nv84_crypt_create_context;
			engine->crypt.destroy_context = nv84_crypt_destroy_context;
			break;
		default:
			engine->crypt.init	= nouveau_stub_init;
			engine->crypt.takedown	= nouveau_stub_takedown;
			break;
		}
		break;
	case 0xC0:
		engine->instmem.init		= nvc0_instmem_init;
		engine->instmem.takedown	= nvc0_instmem_takedown;
		engine->instmem.suspend		= nvc0_instmem_suspend;
		engine->instmem.resume		= nvc0_instmem_resume;
		engine->instmem.populate	= nvc0_instmem_populate;
		engine->instmem.clear		= nvc0_instmem_clear;
		engine->instmem.bind		= nvc0_instmem_bind;
		engine->instmem.unbind		= nvc0_instmem_unbind;
		engine->instmem.flush		= nvc0_instmem_flush;
		engine->mc.init			= nv50_mc_init;
		engine->mc.takedown		= nv50_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nvc0_fb_init;
		engine->fb.takedown		= nvc0_fb_takedown;
		engine->graph.init		= nvc0_graph_init;
		engine->graph.takedown		= nvc0_graph_takedown;
		engine->graph.fifo_access	= nvc0_graph_fifo_access;
		engine->graph.channel		= nvc0_graph_channel;
		engine->graph.create_context	= nvc0_graph_create_context;
		engine->graph.destroy_context	= nvc0_graph_destroy_context;
		engine->graph.load_context	= nvc0_graph_load_context;
		engine->graph.unload_context	= nvc0_graph_unload_context;
		engine->fifo.channels		= 128;
		engine->fifo.init		= nvc0_fifo_init;
		engine->fifo.takedown		= nvc0_fifo_takedown;
		engine->fifo.disable		= nvc0_fifo_disable;
		engine->fifo.enable		= nvc0_fifo_enable;
		engine->fifo.reassign		= nvc0_fifo_reassign;
		engine->fifo.channel_id		= nvc0_fifo_channel_id;
		engine->fifo.create_context	= nvc0_fifo_create_context;
		engine->fifo.destroy_context	= nvc0_fifo_destroy_context;
		engine->fifo.load_context	= nvc0_fifo_load_context;
		engine->fifo.unload_context	= nvc0_fifo_unload_context;
		engine->display.early_init	= nv50_display_early_init;
		engine->display.late_takedown	= nv50_display_late_takedown;
		engine->display.create		= nv50_display_create;
		engine->display.init		= nv50_display_init;
		engine->display.destroy		= nv50_display_destroy;
		engine->gpio.init		= nv50_gpio_init;
		engine->gpio.takedown		= nouveau_stub_takedown;
		engine->gpio.get		= nv50_gpio_get;
		engine->gpio.set		= nv50_gpio_set;
		engine->gpio.irq_enable		= nv50_gpio_irq_enable;
		engine->crypt.init		= nouveau_stub_init;
		engine->crypt.takedown		= nouveau_stub_takedown;
		break;
	default:
		NV_ERROR(dev, "NV%02x unsupported\n", dev_priv->chipset);
		return 1;
	}

	return 0;
}

static unsigned int
nouveau_vga_set_decode(void *priv, bool state)
{
	struct drm_device *dev = priv;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->chipset >= 0x40)
		nv_wr32(dev, 0x88054, state);
	else
		nv_wr32(dev, 0x1854, state);

	if (state)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

static int
nouveau_card_init_channel(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = NULL;
	int ret;

	ret = nouveau_channel_alloc(dev, &dev_priv->channel,
				    (struct drm_file *)-2, NvDmaFB, NvDmaTT);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_dma_new(dev_priv->channel, NV_CLASS_DMA_IN_MEMORY,
				     0, dev_priv->vram_size,
				     NV_DMA_ACCESS_RW, NV_DMA_TARGET_VIDMEM,
				     &gpuobj);
	if (ret)
		goto out_err;

	ret = nouveau_ramht_insert(dev_priv->channel, NvDmaVRAM, gpuobj);
	nouveau_gpuobj_ref(NULL, &gpuobj);
	if (ret)
		goto out_err;

	ret = nouveau_gpuobj_gart_dma_new(dev_priv->channel, 0,
					  dev_priv->gart_info.aper_size,
					  NV_DMA_ACCESS_RW, &gpuobj, NULL);
	if (ret)
		goto out_err;

	ret = nouveau_ramht_insert(dev_priv->channel, NvDmaGART, gpuobj);
	nouveau_gpuobj_ref(NULL, &gpuobj);
	if (ret)
		goto out_err;

	mutex_unlock(&dev_priv->channel->mutex);
	return 0;

out_err:
	nouveau_channel_put(&dev_priv->channel);
	return ret;
}

static void nouveau_switcheroo_set_state(struct pci_dev *pdev,
					 enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	pm_message_t pmm = { .event = PM_EVENT_SUSPEND };
	if (state == VGA_SWITCHEROO_ON) {
		printk(KERN_ERR "VGA switcheroo: switched nouveau on\n");
		nouveau_pci_resume(pdev);
		drm_kms_helper_poll_enable(dev);
	} else {
		printk(KERN_ERR "VGA switcheroo: switched nouveau off\n");
		drm_kms_helper_poll_disable(dev);
		nouveau_pci_suspend(pdev, pmm);
	}
}

static bool nouveau_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	bool can_switch;

	spin_lock(&dev->count_lock);
	can_switch = (dev->open_count == 0);
	spin_unlock(&dev->count_lock);
	return can_switch;
}

int
nouveau_card_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine;
	int ret;

	vga_client_register(dev->pdev, dev, NULL, nouveau_vga_set_decode);
	vga_switcheroo_register_client(dev->pdev, nouveau_switcheroo_set_state,
				       nouveau_switcheroo_can_switch);

	/* Initialise internal driver API hooks */
	ret = nouveau_init_engine_ptrs(dev);
	if (ret)
		goto out;
	engine = &dev_priv->engine;
	spin_lock_init(&dev_priv->channels.lock);
	spin_lock_init(&dev_priv->context_switch_lock);

	/* Make the CRTCs and I2C buses accessible */
	ret = engine->display.early_init(dev);
	if (ret)
		goto out;

	/* Parse BIOS tables / Run init tables if card not POSTed */
	ret = nouveau_bios_init(dev);
	if (ret)
		goto out_display_early;

	nouveau_pm_init(dev);

	ret = nouveau_mem_vram_init(dev);
	if (ret)
		goto out_bios;

	ret = nouveau_gpuobj_init(dev);
	if (ret)
		goto out_vram;

	ret = engine->instmem.init(dev);
	if (ret)
		goto out_gpuobj;

	ret = nouveau_mem_gart_init(dev);
	if (ret)
		goto out_instmem;

	/* PMC */
	ret = engine->mc.init(dev);
	if (ret)
		goto out_gart;

	/* PGPIO */
	ret = engine->gpio.init(dev);
	if (ret)
		goto out_mc;

	/* PTIMER */
	ret = engine->timer.init(dev);
	if (ret)
		goto out_gpio;

	/* PFB */
	ret = engine->fb.init(dev);
	if (ret)
		goto out_timer;

	if (nouveau_noaccel)
		engine->graph.accel_blocked = true;
	else {
		/* PGRAPH */
		ret = engine->graph.init(dev);
		if (ret)
			goto out_fb;

		/* PCRYPT */
		ret = engine->crypt.init(dev);
		if (ret)
			goto out_graph;

		/* PFIFO */
		ret = engine->fifo.init(dev);
		if (ret)
			goto out_crypt;
	}

	ret = engine->display.create(dev);
	if (ret)
		goto out_fifo;

	ret = drm_vblank_init(dev, nv_two_heads(dev) ? 2 : 1);
	if (ret)
		goto out_vblank;

	ret = nouveau_irq_init(dev);
	if (ret)
		goto out_vblank;

	/* what about PVIDEO/PCRTC/PRAMDAC etc? */

	if (!engine->graph.accel_blocked) {
		ret = nouveau_fence_init(dev);
		if (ret)
			goto out_irq;

		ret = nouveau_card_init_channel(dev);
		if (ret)
			goto out_fence;
	}

	ret = nouveau_backlight_init(dev);
	if (ret)
		NV_ERROR(dev, "Error %d registering backlight\n", ret);

	nouveau_fbcon_init(dev);
	drm_kms_helper_poll_init(dev);
	return 0;

out_fence:
	nouveau_fence_fini(dev);
out_irq:
	nouveau_irq_fini(dev);
out_vblank:
	drm_vblank_cleanup(dev);
	engine->display.destroy(dev);
out_fifo:
	if (!nouveau_noaccel)
		engine->fifo.takedown(dev);
out_crypt:
	if (!nouveau_noaccel)
		engine->crypt.takedown(dev);
out_graph:
	if (!nouveau_noaccel)
		engine->graph.takedown(dev);
out_fb:
	engine->fb.takedown(dev);
out_timer:
	engine->timer.takedown(dev);
out_gpio:
	engine->gpio.takedown(dev);
out_mc:
	engine->mc.takedown(dev);
out_gart:
	nouveau_mem_gart_fini(dev);
out_instmem:
	engine->instmem.takedown(dev);
out_gpuobj:
	nouveau_gpuobj_takedown(dev);
out_vram:
	nouveau_mem_vram_fini(dev);
out_bios:
	nouveau_pm_fini(dev);
	nouveau_bios_takedown(dev);
out_display_early:
	engine->display.late_takedown(dev);
out:
	vga_client_register(dev->pdev, NULL, NULL, NULL);
	return ret;
}

static void nouveau_card_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;

	nouveau_backlight_exit(dev);

	if (!engine->graph.accel_blocked) {
		nouveau_fence_fini(dev);
		nouveau_channel_put_unlocked(&dev_priv->channel);
	}

	if (!nouveau_noaccel) {
		engine->fifo.takedown(dev);
		engine->crypt.takedown(dev);
		engine->graph.takedown(dev);
	}
	engine->fb.takedown(dev);
	engine->timer.takedown(dev);
	engine->gpio.takedown(dev);
	engine->mc.takedown(dev);
	engine->display.late_takedown(dev);

	mutex_lock(&dev->struct_mutex);
	ttm_bo_clean_mm(&dev_priv->ttm.bdev, TTM_PL_VRAM);
	ttm_bo_clean_mm(&dev_priv->ttm.bdev, TTM_PL_TT);
	mutex_unlock(&dev->struct_mutex);
	nouveau_mem_gart_fini(dev);

	engine->instmem.takedown(dev);
	nouveau_gpuobj_takedown(dev);
	nouveau_mem_vram_fini(dev);

	nouveau_irq_fini(dev);
	drm_vblank_cleanup(dev);

	nouveau_pm_fini(dev);
	nouveau_bios_takedown(dev);

	vga_client_register(dev->pdev, NULL, NULL, NULL);
}

/* here a client dies, release the stuff that was allocated for its
 * file_priv */
void nouveau_preclose(struct drm_device *dev, struct drm_file *file_priv)
{
	nouveau_channel_cleanup(dev, file_priv);
}

/* first module load, setup the mmio/fb mapping */
/* KMS: we need mmio at load time, not when the first drm client opens. */
int nouveau_firstopen(struct drm_device *dev)
{
	return 0;
}

/* if we have an OF card, copy vbios to RAMIN */
static void nouveau_OF_copy_vbios_to_ramin(struct drm_device *dev)
{
#if defined(__powerpc__)
	int size, i;
	const uint32_t *bios;
	struct device_node *dn = pci_device_to_OF_node(dev->pdev);
	if (!dn) {
		NV_INFO(dev, "Unable to get the OF node\n");
		return;
	}

	bios = of_get_property(dn, "NVDA,BMP", &size);
	if (bios) {
		for (i = 0; i < size; i += 4)
			nv_wi32(dev, i, bios[i/4]);
		NV_INFO(dev, "OF bios successfully copied (%d bytes)\n", size);
	} else {
		NV_INFO(dev, "Unable to get the OF bios\n");
	}
#endif
}

static struct apertures_struct *nouveau_get_apertures(struct drm_device *dev)
{
	struct pci_dev *pdev = dev->pdev;
	struct apertures_struct *aper = alloc_apertures(3);
	if (!aper)
		return NULL;

	aper->ranges[0].base = pci_resource_start(pdev, 1);
	aper->ranges[0].size = pci_resource_len(pdev, 1);
	aper->count = 1;

	if (pci_resource_len(pdev, 2)) {
		aper->ranges[aper->count].base = pci_resource_start(pdev, 2);
		aper->ranges[aper->count].size = pci_resource_len(pdev, 2);
		aper->count++;
	}

	if (pci_resource_len(pdev, 3)) {
		aper->ranges[aper->count].base = pci_resource_start(pdev, 3);
		aper->ranges[aper->count].size = pci_resource_len(pdev, 3);
		aper->count++;
	}

	return aper;
}

static int nouveau_remove_conflicting_drivers(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	bool primary = false;
	dev_priv->apertures = nouveau_get_apertures(dev);
	if (!dev_priv->apertures)
		return -ENOMEM;

#ifdef CONFIG_X86
	primary = dev->pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;
#endif
	
	remove_conflicting_framebuffers(dev_priv->apertures, "nouveaufb", primary);
	return 0;
}

int nouveau_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_nouveau_private *dev_priv;
	uint32_t reg0;
	resource_size_t mmio_start_offs;
	int ret;

	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (!dev_priv) {
		ret = -ENOMEM;
		goto err_out;
	}
	dev->dev_private = dev_priv;
	dev_priv->dev = dev;

	dev_priv->flags = flags & NOUVEAU_FLAGS;

	NV_DEBUG(dev, "vendor: 0x%X device: 0x%X class: 0x%X\n",
		 dev->pci_vendor, dev->pci_device, dev->pdev->class);

	dev_priv->wq = create_workqueue("nouveau");
	if (!dev_priv->wq) {
		ret = -EINVAL;
		goto err_priv;
	}

	/* resource 0 is mmio regs */
	/* resource 1 is linear FB */
	/* resource 2 is RAMIN (mmio regs + 0x1000000) */
	/* resource 6 is bios */

	/* map the mmio regs */
	mmio_start_offs = pci_resource_start(dev->pdev, 0);
	dev_priv->mmio = ioremap(mmio_start_offs, 0x00800000);
	if (!dev_priv->mmio) {
		NV_ERROR(dev, "Unable to initialize the mmio mapping. "
			 "Please report your setup to " DRIVER_EMAIL "\n");
		ret = -EINVAL;
		goto err_wq;
	}
	NV_DEBUG(dev, "regs mapped ok at 0x%llx\n",
					(unsigned long long)mmio_start_offs);

#ifdef __BIG_ENDIAN
	/* Put the card in BE mode if it's not */
	if (nv_rd32(dev, NV03_PMC_BOOT_1))
		nv_wr32(dev, NV03_PMC_BOOT_1, 0x00000001);

	DRM_MEMORYBARRIER();
#endif

	/* Time to determine the card architecture */
	reg0 = nv_rd32(dev, NV03_PMC_BOOT_0);

	/* We're dealing with >=NV10 */
	if ((reg0 & 0x0f000000) > 0) {
		/* Bit 27-20 contain the architecture in hex */
		dev_priv->chipset = (reg0 & 0xff00000) >> 20;
	/* NV04 or NV05 */
	} else if ((reg0 & 0xff00fff0) == 0x20004000) {
		if (reg0 & 0x00f00000)
			dev_priv->chipset = 0x05;
		else
			dev_priv->chipset = 0x04;
	} else
		dev_priv->chipset = 0xff;

	switch (dev_priv->chipset & 0xf0) {
	case 0x00:
	case 0x10:
	case 0x20:
	case 0x30:
		dev_priv->card_type = dev_priv->chipset & 0xf0;
		break;
	case 0x40:
	case 0x60:
		dev_priv->card_type = NV_40;
		break;
	case 0x50:
	case 0x80:
	case 0x90:
	case 0xa0:
		dev_priv->card_type = NV_50;
		break;
	case 0xc0:
		dev_priv->card_type = NV_C0;
		break;
	default:
		NV_INFO(dev, "Unsupported chipset 0x%08x\n", reg0);
		ret = -EINVAL;
		goto err_mmio;
	}

	NV_INFO(dev, "Detected an NV%2x generation card (0x%08x)\n",
		dev_priv->card_type, reg0);

	ret = nouveau_remove_conflicting_drivers(dev);
	if (ret)
		goto err_mmio;

	/* Map PRAMIN BAR, or on older cards, the aperture withing BAR0 */
	if (dev_priv->card_type >= NV_40) {
		int ramin_bar = 2;
		if (pci_resource_len(dev->pdev, ramin_bar) == 0)
			ramin_bar = 3;

		dev_priv->ramin_size = pci_resource_len(dev->pdev, ramin_bar);
		dev_priv->ramin =
			ioremap(pci_resource_start(dev->pdev, ramin_bar),
				dev_priv->ramin_size);
		if (!dev_priv->ramin) {
			NV_ERROR(dev, "Failed to PRAMIN BAR");
			ret = -ENOMEM;
			goto err_mmio;
		}
	} else {
		dev_priv->ramin_size = 1 * 1024 * 1024;
		dev_priv->ramin = ioremap(mmio_start_offs + NV_RAMIN,
					  dev_priv->ramin_size);
		if (!dev_priv->ramin) {
			NV_ERROR(dev, "Failed to map BAR0 PRAMIN.\n");
			ret = -ENOMEM;
			goto err_mmio;
		}
	}

	nouveau_OF_copy_vbios_to_ramin(dev);

	/* Special flags */
	if (dev->pci_device == 0x01a0)
		dev_priv->flags |= NV_NFORCE;
	else if (dev->pci_device == 0x01f0)
		dev_priv->flags |= NV_NFORCE2;

	/* For kernel modesetting, init card now and bring up fbcon */
	ret = nouveau_card_init(dev);
	if (ret)
		goto err_ramin;

	return 0;

err_ramin:
	iounmap(dev_priv->ramin);
err_mmio:
	iounmap(dev_priv->mmio);
err_wq:
	destroy_workqueue(dev_priv->wq);
err_priv:
	kfree(dev_priv);
	dev->dev_private = NULL;
err_out:
	return ret;
}

void nouveau_lastclose(struct drm_device *dev)
{
}

int nouveau_unload(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;

	drm_kms_helper_poll_fini(dev);
	nouveau_fbcon_fini(dev);
	engine->display.destroy(dev);
	nouveau_card_takedown(dev);

	iounmap(dev_priv->mmio);
	iounmap(dev_priv->ramin);

	kfree(dev_priv);
	dev->dev_private = NULL;
	return 0;
}

int nouveau_ioctl_getparam(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_getparam *getparam = data;

	switch (getparam->param) {
	case NOUVEAU_GETPARAM_CHIPSET_ID:
		getparam->value = dev_priv->chipset;
		break;
	case NOUVEAU_GETPARAM_PCI_VENDOR:
		getparam->value = dev->pci_vendor;
		break;
	case NOUVEAU_GETPARAM_PCI_DEVICE:
		getparam->value = dev->pci_device;
		break;
	case NOUVEAU_GETPARAM_BUS_TYPE:
		if (drm_device_is_agp(dev))
			getparam->value = NV_AGP;
		else if (drm_device_is_pcie(dev))
			getparam->value = NV_PCIE;
		else
			getparam->value = NV_PCI;
		break;
	case NOUVEAU_GETPARAM_FB_PHYSICAL:
		getparam->value = dev_priv->fb_phys;
		break;
	case NOUVEAU_GETPARAM_AGP_PHYSICAL:
		getparam->value = dev_priv->gart_info.aper_base;
		break;
	case NOUVEAU_GETPARAM_PCI_PHYSICAL:
		if (dev->sg) {
			getparam->value = (unsigned long)dev->sg->virtual;
		} else {
			NV_ERROR(dev, "Requested PCIGART address, "
					"while no PCIGART was created\n");
			return -EINVAL;
		}
		break;
	case NOUVEAU_GETPARAM_FB_SIZE:
		getparam->value = dev_priv->fb_available_size;
		break;
	case NOUVEAU_GETPARAM_AGP_SIZE:
		getparam->value = dev_priv->gart_info.aper_size;
		break;
	case NOUVEAU_GETPARAM_VM_VRAM_BASE:
		getparam->value = dev_priv->vm_vram_base;
		break;
	case NOUVEAU_GETPARAM_PTIMER_TIME:
		getparam->value = dev_priv->engine.timer.read(dev);
		break;
	case NOUVEAU_GETPARAM_HAS_BO_USAGE:
		getparam->value = 1;
		break;
	case NOUVEAU_GETPARAM_GRAPH_UNITS:
		/* NV40 and NV50 versions are quite different, but register
		 * address is the same. User is supposed to know the card
		 * family anyway... */
		if (dev_priv->chipset >= 0x40) {
			getparam->value = nv_rd32(dev, NV40_PMC_GRAPH_UNITS);
			break;
		}
		/* FALLTHRU */
	default:
		NV_DEBUG(dev, "unknown parameter %lld\n", getparam->param);
		return -EINVAL;
	}

	return 0;
}

int
nouveau_ioctl_setparam(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_nouveau_setparam *setparam = data;

	switch (setparam->param) {
	default:
		NV_DEBUG(dev, "unknown parameter %lld\n", setparam->param);
		return -EINVAL;
	}

	return 0;
}

/* Wait until (value(reg) & mask) == val, up until timeout has hit */
bool nouveau_wait_until(struct drm_device *dev, uint64_t timeout,
			uint32_t reg, uint32_t mask, uint32_t val)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	uint64_t start = ptimer->read(dev);

	do {
		if ((nv_rd32(dev, reg) & mask) == val)
			return true;
	} while (ptimer->read(dev) - start < timeout);

	return false;
}

/* Waits for PGRAPH to go completely idle */
bool nouveau_wait_for_idle(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t mask = ~0;

	if (dev_priv->card_type == NV_40)
		mask &= ~NV40_PGRAPH_STATUS_SYNC_STALL;

	if (!nv_wait(dev, NV04_PGRAPH_STATUS, mask, 0)) {
		NV_ERROR(dev, "PGRAPH idle timed out with status 0x%08x\n",
			 nv_rd32(dev, NV04_PGRAPH_STATUS));
		return false;
	}

	return true;
}

