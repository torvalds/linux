/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  auxiliary functions
 *
 *  Authors: Christoph Raisch <raisch@de.ibm.com>
 *           Hoang-Nam Nguyen <hnguyen@de.ibm.com>
 *           Khadija Souissi <souissik@de.ibm.com>
 *           Waleri Fomin <fomin@de.ibm.com>
 *           Heiko J Schick <schickhj@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef EHCA_TOOLS_H
#define EHCA_TOOLS_H

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/idr.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/device.h>

#include <asm/atomic.h>
#include <asm/abs_addr.h>
#include <asm/ibmebus.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/hvcall.h>

extern int ehca_debug_level;

#define ehca_dbg(ib_dev, format, arg...) \
	do { \
		if (unlikely(ehca_debug_level)) \
			dev_printk(KERN_DEBUG, (ib_dev)->dma_device, \
				   "PU%04x EHCA_DBG:%s " format "\n", \
				   raw_smp_processor_id(), __func__, \
				   ## arg); \
	} while (0)

#define ehca_info(ib_dev, format, arg...) \
	dev_info((ib_dev)->dma_device, "PU%04x EHCA_INFO:%s " format "\n", \
		 raw_smp_processor_id(), __func__, ## arg)

#define ehca_warn(ib_dev, format, arg...) \
	dev_warn((ib_dev)->dma_device, "PU%04x EHCA_WARN:%s " format "\n", \
		 raw_smp_processor_id(), __func__, ## arg)

#define ehca_err(ib_dev, format, arg...) \
	dev_err((ib_dev)->dma_device, "PU%04x EHCA_ERR:%s " format "\n", \
		raw_smp_processor_id(), __func__, ## arg)

/* use this one only if no ib_dev available */
#define ehca_gen_dbg(format, arg...) \
	do { \
		if (unlikely(ehca_debug_level)) \
			printk(KERN_DEBUG "PU%04x EHCA_DBG:%s " format "\n", \
			       raw_smp_processor_id(), __func__, ## arg); \
	} while (0)

#define ehca_gen_warn(format, arg...) \
	printk(KERN_INFO "PU%04x EHCA_WARN:%s " format "\n", \
	       raw_smp_processor_id(), __func__, ## arg)

#define ehca_gen_err(format, arg...) \
	printk(KERN_ERR "PU%04x EHCA_ERR:%s " format "\n", \
	       raw_smp_processor_id(), __func__, ## arg)

/**
 * ehca_dmp - printk a memory block, whose length is n*8 bytes.
 * Each line has the following layout:
 * <format string> adr=X ofs=Y <8 bytes hex> <8 bytes hex>
 */
#define ehca_dmp(adr, len, format, args...) \
	do { \
		unsigned int x; \
		unsigned int l = (unsigned int)(len); \
		unsigned char *deb = (unsigned char *)(adr); \
		for (x = 0; x < l; x += 16) { \
			printk(KERN_INFO "EHCA_DMP:%s " format \
			       " adr=%p ofs=%04x %016lx %016lx\n", \
			       __func__, ##args, deb, x, \
			       *((u64 *)&deb[0]), *((u64 *)&deb[8])); \
			deb += 16; \
		} \
	} while (0)

/* define a bitmask, little endian version */
#define EHCA_BMASK(pos, length) (((pos) << 16) + (length))

/* define a bitmask, the ibm way... */
#define EHCA_BMASK_IBM(from, to) (((63 - to) << 16) + ((to) - (from) + 1))

/* internal function, don't use */
#define EHCA_BMASK_SHIFTPOS(mask) (((mask) >> 16) & 0xffff)

/* internal function, don't use */
#define EHCA_BMASK_MASK(mask) (~0ULL >> ((64 - (mask)) & 0xffff))

/**
 * EHCA_BMASK_SET - return value shifted and masked by mask
 * variable|=EHCA_BMASK_SET(MY_MASK,0x4711) ORs the bits in variable
 * variable&=~EHCA_BMASK_SET(MY_MASK,-1) clears the bits from the mask
 * in variable
 */
#define EHCA_BMASK_SET(mask, value) \
	((EHCA_BMASK_MASK(mask) & ((u64)(value))) << EHCA_BMASK_SHIFTPOS(mask))

/**
 * EHCA_BMASK_GET - extract a parameter from value by mask
 */
#define EHCA_BMASK_GET(mask, value) \
	(EHCA_BMASK_MASK(mask) & (((u64)(value)) >> EHCA_BMASK_SHIFTPOS(mask)))

/* Converts ehca to ib return code */
int ehca2ib_return_code(u64 ehca_rc);

#endif /* EHCA_TOOLS_H */
