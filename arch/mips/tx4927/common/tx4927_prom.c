/*
 * linux/arch/mips/tx4927/common/tx4927_prom.c
 *
 * common tx4927 memory interface
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * Copyright 2001-2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/tx4927/tx4927.h>

static unsigned int __init tx4927_process_sdccr(u64 * addr)
{
	u64 val;
	unsigned int sdccr_ce;
	unsigned int sdccr_bs;
	unsigned int sdccr_rs;
	unsigned int sdccr_cs;
	unsigned int sdccr_mw;
	unsigned int bs = 0;
	unsigned int rs = 0;
	unsigned int cs = 0;
	unsigned int mw = 0;
	unsigned int msize = 0;

	val = (*((vu64 *) (addr)));

	/* MVMCP -- need #defs for these bits masks */
	sdccr_ce = ((val & (1 << 10)) >> 10);
	sdccr_bs = ((val & (1 << 8)) >> 8);
	sdccr_rs = ((val & (3 << 5)) >> 5);
	sdccr_cs = ((val & (3 << 2)) >> 2);
	sdccr_mw = ((val & (1 << 0)) >> 0);

	if (sdccr_ce) {
		switch (sdccr_bs) {
		case 0:{
				bs = 2;
				break;
			}
		case 1:{
				bs = 4;
				break;
			}
		}
		switch (sdccr_rs) {
		case 0:{
				rs = 2048;
				break;
			}
		case 1:{
				rs = 4096;
				break;
			}
		case 2:{
				rs = 8192;
				break;
			}
		case 3:{
				rs = 0;
				break;
			}
		}
		switch (sdccr_cs) {
		case 0:{
				cs = 256;
				break;
			}
		case 1:{
				cs = 512;
				break;
			}
		case 2:{
				cs = 1024;
				break;
			}
		case 3:{
				cs = 2048;
				break;
			}
		}
		switch (sdccr_mw) {
		case 0:{
				mw = 8;
				break;
			}	/* 8 bytes = 64 bits */
		case 1:{
				mw = 4;
				break;
			}	/* 4 bytes = 32 bits */
		}
	}

	/*            bytes per chip     MB per chip      num chips */
	msize = (((rs * cs * mw) / (1024 * 1024)) * bs);

	return (msize);
}


unsigned int __init tx4927_get_mem_size(void)
{
	unsigned int c0;
	unsigned int c1;
	unsigned int c2;
	unsigned int c3;
	unsigned int total;

	/* MVMCP -- need #defs for these registers */
	c0 = tx4927_process_sdccr((u64 *) 0xff1f8000);
	c1 = tx4927_process_sdccr((u64 *) 0xff1f8008);
	c2 = tx4927_process_sdccr((u64 *) 0xff1f8010);
	c3 = tx4927_process_sdccr((u64 *) 0xff1f8018);
	total = c0 + c1 + c2 + c3;

	return (total);
}
