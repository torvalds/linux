###############################################################################
#
# MN10300 Low-level cache purging routines for gdbstub
#
# Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
# Written by David Howells (dhowells@redhat.com)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public Licence
# as published by the Free Software Foundation; either version
# 2 of the Licence, or (at your option) any later version.
#
###############################################################################
#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/smp.h>
#include <asm/cache.h>
#include <asm/cpu-regs.h>
#include <asm/exceptions.h>
#include <asm/frame.inc>
#include <asm/serial-regs.h>

	.text

###############################################################################
#
# GDB stub cache purge
#
###############################################################################
	.type	gdbstub_purge_cache,@function
ENTRY(gdbstub_purge_cache)
	#######################################################################
	# read the addresses tagged in the cache's tag RAM and attempt to flush
	# those addresses specifically
	# - we rely on the hardware to filter out invalid tag entry addresses
	mov	DCACHE_TAG(0,0),a0		# dcache tag RAM access address
	mov	DCACHE_PURGE(0,0),a1		# dcache purge request address
	mov	L1_CACHE_NWAYS*L1_CACHE_NENTRIES,d1  # total number of entries

mn10300_dcache_flush_loop:
	mov	(a0),d0
	and	L1_CACHE_TAG_ADDRESS|L1_CACHE_TAG_ENTRY,d0
	or	L1_CACHE_TAG_VALID,d0		# retain valid entries in the
						# cache
	mov	d0,(a1)				# conditional purge

mn10300_dcache_flush_skip:
	add	L1_CACHE_BYTES,a0
	add	L1_CACHE_BYTES,a1
	add	-1,d1
	bne	mn10300_dcache_flush_loop

;; 	# unconditionally flush and invalidate the dcache
;; 	mov	DCACHE_PURGE(0,0),a1		# dcache purge request address
;; 	mov	L1_CACHE_NWAYS*L1_CACHE_NENTRIES,d1	# total number of
;;							# entries
;;
;; gdbstub_purge_cache__dcache_loop:
;; 	mov	(a1),d0				# unconditional purge
;;
;; 	add	L1_CACHE_BYTES,a1
;; 	add	-1,d1
;; 	bne	gdbstub_purge_cache__dcache_loop

	#######################################################################
	# now invalidate the icache
	mov	CHCTR,a0
	movhu	(a0),a1

	mov	epsw,d1
	and	~EPSW_IE,epsw
	nop
	nop

	# disable the icache
	and	~CHCTR_ICEN,d0
	movhu	d0,(a0)

	# and wait for it to calm down
	setlb
	movhu	(a0),d0
	btst	CHCTR_ICBUSY,d0
	lne

	# invalidate
	or	CHCTR_ICINV,d0
	movhu	d0,(a0)

	# wait for the cache to finish
	mov	CHCTR,a0
	setlb
	movhu	(a0),d0
	btst	CHCTR_ICBUSY,d0
	lne

	# and reenable it
	movhu	a1,(a0)
	movhu	(a0),d0			# read back to flush
					# (SIGILLs all over without this)

	mov	d1,epsw

	ret	[],0

	.size	gdbstub_purge_cache,.-gdbstub_purge_cache
