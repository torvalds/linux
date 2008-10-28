/*
 * linux/arch/mips/txx9/generic/mem_tx4927.c
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
#include <linux/types.h>
#include <linux/io.h>
#include <asm/txx9/tx4927.h>

static unsigned int __init tx4927_process_sdccr(u64 __iomem *addr)
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

	val = __raw_readq(addr);

	/* MVMCP -- need #defs for these bits masks */
	sdccr_ce = ((val & (1 << 10)) >> 10);
	sdccr_bs = ((val & (1 << 8)) >> 8);
	sdccr_rs = ((val & (3 << 5)) >> 5);
	sdccr_cs = ((val & (7 << 2)) >> 2);
	sdccr_mw = ((val & (1 << 0)) >> 0);

	if (sdccr_ce) {
		bs = 2 << sdccr_bs;
		rs = 2048 << sdccr_rs;
		cs = 256 << sdccr_cs;
		mw = 8 >> sdccr_mw;
	}

	return rs * cs * mw * bs;
}

unsigned int __init tx4927_get_mem_size(void)
{
	unsigned int total = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(tx4927_sdramcptr->cr); i++)
		total += tx4927_process_sdccr(&tx4927_sdramcptr->cr[i]);
	return total;
}
