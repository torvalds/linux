/*
 *  Copyright (C) 2004 Florian Schirmer <jolt@tuxbox.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/init.h>
#include <linux/ssb/ssb.h>
#include <asm/time.h>
#include <bcm47xx.h>
#include <bcm47xx_nvram.h>
#include <bcm47xx_board.h>

void __init plat_time_init(void)
{
	unsigned long hz = 0;
	u16 chip_id = 0;
	char buf[10];
	int len;
	enum bcm47xx_board board = bcm47xx_board_get();

	/*
	 * Use deterministic values for initial counter interrupt
	 * so that calibrate delay avoids encountering a counter wrap.
	 */
	write_c0_count(0);
	write_c0_compare(0xffff);

	switch (bcm47xx_bus_type) {
#ifdef CONFIG_BCM47XX_SSB
	case BCM47XX_BUS_TYPE_SSB:
		hz = ssb_cpu_clock(&bcm47xx_bus.ssb.mipscore) / 2;
		chip_id = bcm47xx_bus.ssb.chip_id;
		break;
#endif
#ifdef CONFIG_BCM47XX_BCMA
	case BCM47XX_BUS_TYPE_BCMA:
		hz = bcma_cpu_clock(&bcm47xx_bus.bcma.bus.drv_mips) / 2;
		chip_id = bcm47xx_bus.bcma.bus.chipinfo.id;
		break;
#endif
	}

	if (chip_id == 0x5354) {
		len = bcm47xx_nvram_getenv("clkfreq", buf, sizeof(buf));
		if (len >= 0 && !strncmp(buf, "200", 4))
			hz = 100000000;
	}

	switch (board) {
	case BCM47XX_BOARD_ASUS_WL520GC:
	case BCM47XX_BOARD_ASUS_WL520GU:
		hz = 100000000;
		break;
	default:
		break;
	}

	if (!hz)
		hz = 100000000;

	/* Set MIPS counter frequency for fixed_rate_gettimeoffset() */
	mips_hpt_frequency = hz;
}
