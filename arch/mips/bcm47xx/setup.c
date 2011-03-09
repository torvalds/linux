/*
 *  Copyright (C) 2004 Florian Schirmer <jolt@tuxbox.org>
 *  Copyright (C) 2006 Felix Fietkau <nbd@openwrt.org>
 *  Copyright (C) 2006 Michael Buesch <mb@bu3sch.de>
 *  Copyright (C) 2010 Waldemar Brodkorb <wbx@openadk.org>
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

#include <linux/types.h>
#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_embedded.h>
#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <bcm47xx.h>
#include <asm/mach-bcm47xx/nvram.h>

struct ssb_bus ssb_bcm47xx;
EXPORT_SYMBOL(ssb_bcm47xx);

static void bcm47xx_machine_restart(char *command)
{
	printk(KERN_ALERT "Please stand by while rebooting the system...\n");
	local_irq_disable();
	/* Set the watchdog timer to reset immediately */
	ssb_watchdog_timer_set(&ssb_bcm47xx, 1);
	while (1)
		cpu_relax();
}

static void bcm47xx_machine_halt(void)
{
	/* Disable interrupts and watchdog and spin forever */
	local_irq_disable();
	ssb_watchdog_timer_set(&ssb_bcm47xx, 0);
	while (1)
		cpu_relax();
}

#define READ_FROM_NVRAM(_outvar, name, buf) \
	if (nvram_getenv(name, buf, sizeof(buf)) >= 0)\
		sprom->_outvar = simple_strtoul(buf, NULL, 0);

static void bcm47xx_fill_sprom(struct ssb_sprom *sprom)
{
	char buf[100];
	u32 boardflags;

	memset(sprom, 0, sizeof(struct ssb_sprom));

	sprom->revision = 1; /* Fallback: Old hardware does not define this. */
	READ_FROM_NVRAM(revision, "sromrev", buf);
	if (nvram_getenv("il0macaddr", buf, sizeof(buf)) >= 0)
		nvram_parse_macaddr(buf, sprom->il0mac);
	if (nvram_getenv("et0macaddr", buf, sizeof(buf)) >= 0)
		nvram_parse_macaddr(buf, sprom->et0mac);
	if (nvram_getenv("et1macaddr", buf, sizeof(buf)) >= 0)
		nvram_parse_macaddr(buf, sprom->et1mac);
	READ_FROM_NVRAM(et0phyaddr, "et0phyaddr", buf);
	READ_FROM_NVRAM(et1phyaddr, "et1phyaddr", buf);
	READ_FROM_NVRAM(et0mdcport, "et0mdcport", buf);
	READ_FROM_NVRAM(et1mdcport, "et1mdcport", buf);
	READ_FROM_NVRAM(board_rev, "boardrev", buf);
	READ_FROM_NVRAM(country_code, "ccode", buf);
	READ_FROM_NVRAM(ant_available_a, "aa5g", buf);
	READ_FROM_NVRAM(ant_available_bg, "aa2g", buf);
	READ_FROM_NVRAM(pa0b0, "pa0b0", buf);
	READ_FROM_NVRAM(pa0b1, "pa0b1", buf);
	READ_FROM_NVRAM(pa0b2, "pa0b2", buf);
	READ_FROM_NVRAM(pa1b0, "pa1b0", buf);
	READ_FROM_NVRAM(pa1b1, "pa1b1", buf);
	READ_FROM_NVRAM(pa1b2, "pa1b2", buf);
	READ_FROM_NVRAM(pa1lob0, "pa1lob0", buf);
	READ_FROM_NVRAM(pa1lob2, "pa1lob1", buf);
	READ_FROM_NVRAM(pa1lob1, "pa1lob2", buf);
	READ_FROM_NVRAM(pa1hib0, "pa1hib0", buf);
	READ_FROM_NVRAM(pa1hib2, "pa1hib1", buf);
	READ_FROM_NVRAM(pa1hib1, "pa1hib2", buf);
	READ_FROM_NVRAM(gpio0, "wl0gpio0", buf);
	READ_FROM_NVRAM(gpio1, "wl0gpio1", buf);
	READ_FROM_NVRAM(gpio2, "wl0gpio2", buf);
	READ_FROM_NVRAM(gpio3, "wl0gpio3", buf);
	READ_FROM_NVRAM(maxpwr_bg, "pa0maxpwr", buf);
	READ_FROM_NVRAM(maxpwr_al, "pa1lomaxpwr", buf);
	READ_FROM_NVRAM(maxpwr_a, "pa1maxpwr", buf);
	READ_FROM_NVRAM(maxpwr_ah, "pa1himaxpwr", buf);
	READ_FROM_NVRAM(itssi_a, "pa1itssit", buf);
	READ_FROM_NVRAM(itssi_bg, "pa0itssit", buf);
	READ_FROM_NVRAM(tri2g, "tri2g", buf);
	READ_FROM_NVRAM(tri5gl, "tri5gl", buf);
	READ_FROM_NVRAM(tri5g, "tri5g", buf);
	READ_FROM_NVRAM(tri5gh, "tri5gh", buf);
	READ_FROM_NVRAM(rxpo2g, "rxpo2g", buf);
	READ_FROM_NVRAM(rxpo5g, "rxpo5g", buf);
	READ_FROM_NVRAM(rssisav2g, "rssisav2g", buf);
	READ_FROM_NVRAM(rssismc2g, "rssismc2g", buf);
	READ_FROM_NVRAM(rssismf2g, "rssismf2g", buf);
	READ_FROM_NVRAM(bxa2g, "bxa2g", buf);
	READ_FROM_NVRAM(rssisav5g, "rssisav5g", buf);
	READ_FROM_NVRAM(rssismc5g, "rssismc5g", buf);
	READ_FROM_NVRAM(rssismf5g, "rssismf5g", buf);
	READ_FROM_NVRAM(bxa5g, "bxa5g", buf);
	READ_FROM_NVRAM(cck2gpo, "cck2gpo", buf);
	READ_FROM_NVRAM(ofdm2gpo, "ofdm2gpo", buf);
	READ_FROM_NVRAM(ofdm5glpo, "ofdm5glpo", buf);
	READ_FROM_NVRAM(ofdm5gpo, "ofdm5gpo", buf);
	READ_FROM_NVRAM(ofdm5ghpo, "ofdm5ghpo", buf);

	if (nvram_getenv("boardflags", buf, sizeof(buf)) >= 0) {
		boardflags = simple_strtoul(buf, NULL, 0);
		if (boardflags) {
			sprom->boardflags_lo = (boardflags & 0x0000FFFFU);
			sprom->boardflags_hi = (boardflags & 0xFFFF0000U) >> 16;
		}
	}
	if (nvram_getenv("boardflags2", buf, sizeof(buf)) >= 0) {
		boardflags = simple_strtoul(buf, NULL, 0);
		if (boardflags) {
			sprom->boardflags2_lo = (boardflags & 0x0000FFFFU);
			sprom->boardflags2_hi = (boardflags & 0xFFFF0000U) >> 16;
		}
	}
}

static int bcm47xx_get_invariants(struct ssb_bus *bus,
				   struct ssb_init_invariants *iv)
{
	char buf[20];

	/* Fill boardinfo structure */
	memset(&(iv->boardinfo), 0 , sizeof(struct ssb_boardinfo));

	if (nvram_getenv("boardvendor", buf, sizeof(buf)) >= 0)
		iv->boardinfo.vendor = (u16)simple_strtoul(buf, NULL, 0);
	else
		iv->boardinfo.vendor = SSB_BOARDVENDOR_BCM;
	if (nvram_getenv("boardtype", buf, sizeof(buf)) >= 0)
		iv->boardinfo.type = (u16)simple_strtoul(buf, NULL, 0);
	if (nvram_getenv("boardrev", buf, sizeof(buf)) >= 0)
		iv->boardinfo.rev = (u16)simple_strtoul(buf, NULL, 0);

	bcm47xx_fill_sprom(&iv->sprom);

	if (nvram_getenv("cardbus", buf, sizeof(buf)) >= 0)
		iv->has_cardbus_slot = !!simple_strtoul(buf, NULL, 10);

	return 0;
}

void __init plat_mem_setup(void)
{
	int err;
	char buf[100];
	struct ssb_mipscore *mcore;

	err = ssb_bus_ssbbus_register(&ssb_bcm47xx, SSB_ENUM_BASE,
				      bcm47xx_get_invariants);
	if (err)
		panic("Failed to initialize SSB bus (err %d)\n", err);

	mcore = &ssb_bcm47xx.mipscore;
	if (nvram_getenv("kernel_args", buf, sizeof(buf)) >= 0) {
		if (strstr(buf, "console=ttyS1")) {
			struct ssb_serial_port port;

			printk(KERN_DEBUG "Swapping serial ports!\n");
			/* swap serial ports */
			memcpy(&port, &mcore->serial_ports[0], sizeof(port));
			memcpy(&mcore->serial_ports[0], &mcore->serial_ports[1],
			       sizeof(port));
			memcpy(&mcore->serial_ports[1], &port, sizeof(port));
		}
	}

	_machine_restart = bcm47xx_machine_restart;
	_machine_halt = bcm47xx_machine_halt;
	pm_power_off = bcm47xx_machine_halt;
}
