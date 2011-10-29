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
		engine->instmem.get		= nv04_instmem_get;
		engine->instmem.put		= nv04_instmem_put;
		engine->instmem.map		= nv04_instmem_map;
		engine->instmem.unmap		= nv04_instmem_unmap;
		engine->instmem.flush		= nv04_instmem_flush;
		engine->mc.init			= nv04_mc_init;
		engine->mc.takedown		= nv04_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv04_fb_init;
		engine->fb.takedown		= nv04_fb_takedown;
		engine->fifo.channels		= 16;
		engine->fifo.init		= nv04_fifo_init;
		engine->fifo.takedown		= nv04_fifo_fini;
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
		engine->vram.init		= nouveau_mem_detect;
		engine->vram.takedown		= nouveau_stub_takedown;
		engine->vram.flags_valid	= nouveau_mem_flags_valid;
		break;
	case 0x10:
		engine->instmem.init		= nv04_instmem_init;
		engine->instmem.takedown	= nv04_instmem_takedown;
		engine->instmem.suspend		= nv04_instmem_suspend;
		engine->instmem.resume		= nv04_instmem_resume;
		engine->instmem.get		= nv04_instmem_get;
		engine->instmem.put		= nv04_instmem_put;
		engine->instmem.map		= nv04_instmem_map;
		engine->instmem.unmap		= nv04_instmem_unmap;
		engine->instmem.flush		= nv04_instmem_flush;
		engine->mc.init			= nv04_mc_init;
		engine->mc.takedown		= nv04_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv10_fb_init;
		engine->fb.takedown		= nv10_fb_takedown;
		engine->fb.init_tile_region	= nv10_fb_init_tile_region;
		engine->fb.set_tile_region	= nv10_fb_set_tile_region;
		engine->fb.free_tile_region	= nv10_fb_free_tile_region;
		engine->fifo.channels		= 32;
		engine->fifo.init		= nv10_fifo_init;
		engine->fifo.takedown		= nv04_fifo_fini;
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
		engine->vram.init		= nouveau_mem_detect;
		engine->vram.takedown		= nouveau_stub_takedown;
		engine->vram.flags_valid	= nouveau_mem_flags_valid;
		break;
	case 0x20:
		engine->instmem.init		= nv04_instmem_init;
		engine->instmem.takedown	= nv04_instmem_takedown;
		engine->instmem.suspend		= nv04_instmem_suspend;
		engine->instmem.resume		= nv04_instmem_resume;
		engine->instmem.get		= nv04_instmem_get;
		engine->instmem.put		= nv04_instmem_put;
		engine->instmem.map		= nv04_instmem_map;
		engine->instmem.unmap		= nv04_instmem_unmap;
		engine->instmem.flush		= nv04_instmem_flush;
		engine->mc.init			= nv04_mc_init;
		engine->mc.takedown		= nv04_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv10_fb_init;
		engine->fb.takedown		= nv10_fb_takedown;
		engine->fb.init_tile_region	= nv10_fb_init_tile_region;
		engine->fb.set_tile_region	= nv10_fb_set_tile_region;
		engine->fb.free_tile_region	= nv10_fb_free_tile_region;
		engine->fifo.channels		= 32;
		engine->fifo.init		= nv10_fifo_init;
		engine->fifo.takedown		= nv04_fifo_fini;
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
		engine->vram.init		= nouveau_mem_detect;
		engine->vram.takedown		= nouveau_stub_takedown;
		engine->vram.flags_valid	= nouveau_mem_flags_valid;
		break;
	case 0x30:
		engine->instmem.init		= nv04_instmem_init;
		engine->instmem.takedown	= nv04_instmem_takedown;
		engine->instmem.suspend		= nv04_instmem_suspend;
		engine->instmem.resume		= nv04_instmem_resume;
		engine->instmem.get		= nv04_instmem_get;
		engine->instmem.put		= nv04_instmem_put;
		engine->instmem.map		= nv04_instmem_map;
		engine->instmem.unmap		= nv04_instmem_unmap;
		engine->instmem.flush		= nv04_instmem_flush;
		engine->mc.init			= nv04_mc_init;
		engine->mc.takedown		= nv04_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv30_fb_init;
		engine->fb.takedown		= nv30_fb_takedown;
		engine->fb.init_tile_region	= nv30_fb_init_tile_region;
		engine->fb.set_tile_region	= nv10_fb_set_tile_region;
		engine->fb.free_tile_region	= nv30_fb_free_tile_region;
		engine->fifo.channels		= 32;
		engine->fifo.init		= nv10_fifo_init;
		engine->fifo.takedown		= nv04_fifo_fini;
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
		engine->vram.init		= nouveau_mem_detect;
		engine->vram.takedown		= nouveau_stub_takedown;
		engine->vram.flags_valid	= nouveau_mem_flags_valid;
		break;
	case 0x40:
	case 0x60:
		engine->instmem.init		= nv04_instmem_init;
		engine->instmem.takedown	= nv04_instmem_takedown;
		engine->instmem.suspend		= nv04_instmem_suspend;
		engine->instmem.resume		= nv04_instmem_resume;
		engine->instmem.get		= nv04_instmem_get;
		engine->instmem.put		= nv04_instmem_put;
		engine->instmem.map		= nv04_instmem_map;
		engine->instmem.unmap		= nv04_instmem_unmap;
		engine->instmem.flush		= nv04_instmem_flush;
		engine->mc.init			= nv40_mc_init;
		engine->mc.takedown		= nv40_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nv40_fb_init;
		engine->fb.takedown		= nv40_fb_takedown;
		engine->fb.init_tile_region	= nv30_fb_init_tile_region;
		engine->fb.set_tile_region	= nv40_fb_set_tile_region;
		engine->fb.free_tile_region	= nv30_fb_free_tile_region;
		engine->fifo.channels		= 32;
		engine->fifo.init		= nv40_fifo_init;
		engine->fifo.takedown		= nv04_fifo_fini;
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
		engine->vram.init		= nouveau_mem_detect;
		engine->vram.takedown		= nouveau_stub_takedown;
		engine->vram.flags_valid	= nouveau_mem_flags_valid;
		break;
	case 0x50:
	case 0x80: /* gotta love NVIDIA's consistency.. */
	case 0x90:
	case 0xA0:
		engine->instmem.init		= nv50_instmem_init;
		engine->instmem.takedown	= nv50_instmem_takedown;
		engine->instmem.suspend		= nv50_instmem_suspend;
		engine->instmem.resume		= nv50_instmem_resume;
		engine->instmem.get		= nv50_instmem_get;
		engine->instmem.put		= nv50_instmem_put;
		engine->instmem.map		= nv50_instmem_map;
		engine->instmem.unmap		= nv50_instmem_unmap;
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
		engine->gpio.takedown		= nv50_gpio_fini;
		engine->gpio.get		= nv50_gpio_get;
		engine->gpio.set		= nv50_gpio_set;
		engine->gpio.irq_register	= nv50_gpio_irq_register;
		engine->gpio.irq_unregister	= nv50_gpio_irq_unregister;
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
		engine->vram.init		= nv50_vram_init;
		engine->vram.takedown		= nv50_vram_fini;
		engine->vram.get		= nv50_vram_new;
		engine->vram.put		= nv50_vram_del;
		engine->vram.flags_valid	= nv50_vram_flags_valid;
		break;
	case 0xC0:
		engine->instmem.init		= nvc0_instmem_init;
		engine->instmem.takedown	= nvc0_instmem_takedown;
		engine->instmem.suspend		= nvc0_instmem_suspend;
		engine->instmem.resume		= nvc0_instmem_resume;
		engine->instmem.get		= nv50_instmem_get;
		engine->instmem.put		= nv50_instmem_put;
		engine->instmem.map		= nv50_instmem_map;
		engine->instmem.unmap		= nv50_instmem_unmap;
		engine->instmem.flush		= nv84_instmem_flush;
		engine->mc.init			= nv50_mc_init;
		engine->mc.takedown		= nv50_mc_takedown;
		engine->timer.init		= nv04_timer_init;
		engine->timer.read		= nv04_timer_read;
		engine->timer.takedown		= nv04_timer_takedown;
		engine->fb.init			= nvc0_fb_init;
		engine->fb.takedown		= nvc0_fb_takedown;
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
		engine->gpio.irq_register	= nv50_gpio_irq_register;
		engine->gpio.irq_unregister	= nv50_gpio_irq_unregister;
		engine->gpio.irq_enable		= nv50_gpio_irq_enable;
		engine->vram.init		= nvc0_vram_init;
		engine->vram.takedown		= nv50_vram_fini;
		engine->vram.get		= nvc0_vram_new;
		engine->vram.put		= nv50_vram_del;
		engine->vram.flags_valid	= nvc0_vram_flags_valid;
		engine->pm.temp_get		= nv84_temp_get;
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
	int ret;

	ret = nouveau_channel_alloc(dev, &dev_priv->channel, NULL,
				    NvDmaFB, NvDmaTT);
	if (ret)
		return ret;

	mutex_unlock(&dev_priv->channel->mutex);
	return 0;
}

static void nouveau_switcheroo_set_state(struct pci_dev *pdev,
					 enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	pm_message_t pmm = { .event = PM_EVENT_SUSPEND };
	if (state == VGA_SWITCHEROO_ON) {
		printk(KERN_ERR "VGA switcheroo: switched nouveau on\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		nouveau_pci_resume(pdev);
		drm_kms_helper_poll_enable(dev);
		dev->switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		printk(KERN_ERR "VGA switcheroo: switched nouveau off\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		drm_kms_helper_poll_disable(dev);
		nouveau_pci_suspend(pdev, pmm);
		dev->switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

static void nouveau_switcheroo_reprobe(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	nouveau_fbcon_output_poll_changed(dev);
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
	int ret, e = 0;

	vga_client_register(dev->pdev, dev, NULL, nouveau_vga_set_decode);
	vga_switcheroo_register_client(dev->pdev, nouveau_switcheroo_set_state,
				       nouveau_switcheroo_reprobe,
				       nouveau_switcheroo_can_switch);

	/* Initialise internal driver API hooks */
	ret = nouveau_init_engine_ptrs(dev);
	if (ret)
		goto out;
	engine = &dev_priv->engine;
	spin_lock_init(&dev_priv->channels.lock);
	spin_lock_init(&dev_priv->tile.lock);
	spin_lock_init(&dev_priv->context_switch_lock);
	spin_lock_init(&dev_priv->vm_lock);

	/* Make the CRTCs and I2C buses accessible */
	ret = engine->display.early_init(dev);
	if (ret)
		goto out;

	/* Parse BIOS tables / Run init tables if card not POSTed */
	ret = nouveau_bios_init(dev);
	if (ret)
		goto out_display_early;

	nouveau_pm_init(dev);

	ret = engine->vram.init(dev);
	if (ret)
		goto out_bios;

	ret = nouveau_gpuobj_init(dev);
	if (ret)
		goto out_vram;

	ret = engine->instmem.init(dev);
	if (ret)
		goto out_gpuobj;

	ret = nouveau_mem_vram_init(dev);
	if (ret)
		goto out_instmem;

	ret = nouveau_mem_gart_init(dev);
	if (ret)
		goto out_ttmvram;

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

	if (!dev_priv->noaccel) {
		switch (dev_priv->card_type) {
		case NV_04:
			nv04_graph_create(dev);
			break;
		case NV_10:
			nv10_graph_create(dev);
			break;
		case NV_20:
		case NV_30:
			nv20_graph_create(dev);
			break;
		case NV_40:
			nv40_graph_create(dev);
			break;
		case NV_50:
			nv50_graph_create(dev);
			break;
		case NV_C0:
			nvc0_graph_create(dev);
			break;
		default:
			break;
		}

		switch (dev_priv->chipset) {
		case 0x84:
		case 0x86:
		case 0x92:
		case 0x94:
		case 0x96:
		case 0xa0:
			nv84_crypt_create(dev);
			break;
		}

		switch (dev_priv->card_type) {
		case NV_50:
			switch (dev_priv->chipset) {
			case 0xa3:
			case 0xa5:
			case 0xa8:
			case 0xaf:
				nva3_copy_create(dev);
				break;
			}
			break;
		case NV_C0:
			nvc0_copy_create(dev, 0);
			nvc0_copy_create(dev, 1);
			break;
		default:
			break;
		}

		if (dev_priv->card_type == NV_40)
			nv40_mpeg_create(dev);
		else
		if (dev_priv->card_type == NV_50 &&
		    (dev_priv->chipset < 0x98 || dev_priv->chipset == 0xa0))
			nv50_mpeg_create(dev);

		for (e = 0; e < NVOBJ_ENGINE_NR; e++) {
			if (dev_priv->eng[e]) {
				ret = dev_priv->eng[e]->init(dev, e);
				if (ret)
					goto out_engine;
			}
		}

		/* PFIFO */
		ret = engine->fifo.init(dev);
		if (ret)
			goto out_engine;
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

	if (dev_priv->eng[NVOBJ_ENGINE_GR]) {
		ret = nouveau_fence_init(dev);
		if (ret)
			goto out_irq;

		ret = nouveau_card_init_channel(dev);
		if (ret)
			goto out_fence;
	}

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
	if (!dev_priv->noaccel)
		engine->fifo.takedown(dev);
out_engine:
	if (!dev_priv->noaccel) {
		for (e = e - 1; e >= 0; e--) {
			if (!dev_priv->eng[e])
				continue;
			dev_priv->eng[e]->fini(dev, e, false);
			dev_priv->eng[e]->destroy(dev,e );
		}
	}

	engine->fb.takedown(dev);
out_timer:
	engine->timer.takedown(dev);
out_gpio:
	engine->gpio.takedown(dev);
out_mc:
	engine->mc.takedown(dev);
out_gart:
	nouveau_mem_gart_fini(dev);
out_ttmvram:
	nouveau_mem_vram_fini(dev);
out_instmem:
	engine->instmem.takedown(dev);
out_gpuobj:
	nouveau_gpuobj_takedown(dev);
out_vram:
	engine->vram.takedown(dev);
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
	int e;

	drm_kms_helper_poll_fini(dev);
	nouveau_fbcon_fini(dev);

	if (dev_priv->channel) {
		nouveau_channel_put_unlocked(&dev_priv->channel);
		nouveau_fence_fini(dev);
	}

	engine->display.destroy(dev);

	if (!dev_priv->noaccel) {
		engine->fifo.takedown(dev);
		for (e = NVOBJ_ENGINE_NR - 1; e >= 0; e--) {
			if (dev_priv->eng[e]) {
				dev_priv->eng[e]->fini(dev, e, false);
				dev_priv->eng[e]->destroy(dev,e );
			}
		}
	}
	engine->fb.takedown(dev);
	engine->timer.takedown(dev);
	engine->gpio.takedown(dev);
	engine->mc.takedown(dev);
	engine->display.late_takedown(dev);

	if (dev_priv->vga_ram) {
		nouveau_bo_unpin(dev_priv->vga_ram);
		nouveau_bo_ref(NULL, &dev_priv->vga_ram);
	}

	mutex_lock(&dev->struct_mutex);
	ttm_bo_clean_mm(&dev_priv->ttm.bdev, TTM_PL_VRAM);
	ttm_bo_clean_mm(&dev_priv->ttm.bdev, TTM_PL_TT);
	mutex_unlock(&dev->struct_mutex);
	nouveau_mem_gart_fini(dev);
	nouveau_mem_vram_fini(dev);

	engine->instmem.takedown(dev);
	nouveau_gpuobj_takedown(dev);
	engine->vram.takedown(dev);

	nouveau_irq_fini(dev);
	drm_vblank_cleanup(dev);

	nouveau_pm_fini(dev);
	nouveau_bios_takedown(dev);

	vga_client_register(dev->pdev, NULL, NULL, NULL);
}

int
nouveau_open(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fpriv *fpriv;
	int ret;

	fpriv = kzalloc(sizeof(*fpriv), GFP_KERNEL);
	if (unlikely(!fpriv))
		return -ENOMEM;

	spin_lock_init(&fpriv->lock);
	INIT_LIST_HEAD(&fpriv->channels);

	if (dev_priv->card_type == NV_50) {
		ret = nouveau_vm_new(dev, 0, (1ULL << 40), 0x0020000000ULL,
				     &fpriv->vm);
		if (ret) {
			kfree(fpriv);
			return ret;
		}
	} else
	if (dev_priv->card_type >= NV_C0) {
		ret = nouveau_vm_new(dev, 0, (1ULL << 40), 0x0008000000ULL,
				     &fpriv->vm);
		if (ret) {
			kfree(fpriv);
			return ret;
		}
	}

	file_priv->driver_priv = fpriv;
	return 0;
}

/* here a client dies, release the stuff that was allocated for its
 * file_priv */
void nouveau_preclose(struct drm_device *dev, struct drm_file *file_priv)
{
	nouveau_channel_cleanup(dev, file_priv);
}

void
nouveau_postclose(struct drm_device *dev, struct drm_file *file_priv)
{
	struct nouveau_fpriv *fpriv = nouveau_fpriv(file_priv);
	nouveau_vm_ref(NULL, &fpriv->vm, NULL);
	kfree(fpriv);
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
		goto err_priv;
	}
	NV_DEBUG(dev, "regs mapped ok at 0x%llx\n",
					(unsigned long long)mmio_start_offs);

#ifdef __BIG_ENDIAN
	/* Put the card in BE mode if it's not */
	if (nv_rd32(dev, NV03_PMC_BOOT_1) != 0x01000001)
		nv_wr32(dev, NV03_PMC_BOOT_1, 0x01000001);

	DRM_MEMORYBARRIER();
#endif

	/* Time to determine the card architecture */
	reg0 = nv_rd32(dev, NV03_PMC_BOOT_0);
	dev_priv->stepping = 0; /* XXX: add stepping for pre-NV10? */

	/* We're dealing with >=NV10 */
	if ((reg0 & 0x0f000000) > 0) {
		/* Bit 27-20 contain the architecture in hex */
		dev_priv->chipset = (reg0 & 0xff00000) >> 20;
		dev_priv->stepping = (reg0 & 0xff);
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

	/* Determine whether we'll attempt acceleration or not, some
	 * cards are disabled by default here due to them being known
	 * non-functional, or never been tested due to lack of hw.
	 */
	dev_priv->noaccel = !!nouveau_noaccel;
	if (nouveau_noaccel == -1) {
		switch (dev_priv->chipset) {
		case 0xc1: /* known broken */
		case 0xc8: /* never tested */
			NV_INFO(dev, "acceleration disabled by default, pass "
				     "noaccel=0 to force enable\n");
			dev_priv->noaccel = true;
			break;
		default:
			dev_priv->noaccel = false;
			break;
		}
	}

	ret = nouveau_remove_conflicting_drivers(dev);
	if (ret)
		goto err_mmio;

	/* Map PRAMIN BAR, or on older cards, the aperture within BAR0 */
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
err_priv:
	kfree(dev_priv);
	dev->dev_private = NULL;
err_out:
	return ret;
}

void nouveau_lastclose(struct drm_device *dev)
{
	vga_switcheroo_process_delayed_switch();
}

int nouveau_unload(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

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
		if (drm_pci_device_is_agp(dev))
			getparam->value = NV_AGP;
		else if (pci_is_pcie(dev->pdev))
			getparam->value = NV_PCIE;
		else
			getparam->value = NV_PCI;
		break;
	case NOUVEAU_GETPARAM_FB_SIZE:
		getparam->value = dev_priv->fb_available_size;
		break;
	case NOUVEAU_GETPARAM_AGP_SIZE:
		getparam->value = dev_priv->gart_info.aper_size;
		break;
	case NOUVEAU_GETPARAM_VM_VRAM_BASE:
		getparam->value = 0; /* deprecated */
		break;
	case NOUVEAU_GETPARAM_PTIMER_TIME:
		getparam->value = dev_priv->engine.timer.read(dev);
		break;
	case NOUVEAU_GETPARAM_HAS_BO_USAGE:
		getparam->value = 1;
		break;
	case NOUVEAU_GETPARAM_HAS_PAGEFLIP:
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
bool
nouveau_wait_eq(struct drm_device *dev, uint64_t timeout,
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

/* Wait until (value(reg) & mask) != val, up until timeout has hit */
bool
nouveau_wait_ne(struct drm_device *dev, uint64_t timeout,
		uint32_t reg, uint32_t mask, uint32_t val)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	uint64_t start = ptimer->read(dev);

	do {
		if ((nv_rd32(dev, reg) & mask) != val)
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

