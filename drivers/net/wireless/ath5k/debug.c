/*
 * Copyright (c) 2007-2008 Bruno Randolf <bruno@thinktube.com>
 *
 *  This file is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * Copyright (c) 2004-2005 Atheros Communications, Inc.
 * Copyright (c) 2006 Devicescape Software, Inc.
 * Copyright (c) 2007 Jiri Slaby <jirislaby@gmail.com>
 * Copyright (c) 2007 Luis R. Rodriguez <mcgrof@winlab.rutgers.edu>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include "debug.h"
#include "base.h"

static unsigned int ath5k_debug;
module_param_named(debug, ath5k_debug, uint, 0);


#ifdef CONFIG_ATH5K_DEBUG

#include <linux/seq_file.h>
#include "reg.h"

static struct dentry *ath5k_global_debugfs;

static int ath5k_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}


/* debugfs: registers */

struct reg {
	char *name;
	int addr;
};

#define REG_STRUCT_INIT(r) { #r, r }

/* just a few random registers, might want to add more */
static struct reg regs[] = {
	REG_STRUCT_INIT(AR5K_CR),
	REG_STRUCT_INIT(AR5K_RXDP),
	REG_STRUCT_INIT(AR5K_CFG),
	REG_STRUCT_INIT(AR5K_IER),
	REG_STRUCT_INIT(AR5K_BCR),
	REG_STRUCT_INIT(AR5K_RTSD0),
	REG_STRUCT_INIT(AR5K_RTSD1),
	REG_STRUCT_INIT(AR5K_TXCFG),
	REG_STRUCT_INIT(AR5K_RXCFG),
	REG_STRUCT_INIT(AR5K_RXJLA),
	REG_STRUCT_INIT(AR5K_MIBC),
	REG_STRUCT_INIT(AR5K_TOPS),
	REG_STRUCT_INIT(AR5K_RXNOFRM),
	REG_STRUCT_INIT(AR5K_TXNOFRM),
	REG_STRUCT_INIT(AR5K_RPGTO),
	REG_STRUCT_INIT(AR5K_RFCNT),
	REG_STRUCT_INIT(AR5K_MISC),
	REG_STRUCT_INIT(AR5K_QCUDCU_CLKGT),
	REG_STRUCT_INIT(AR5K_ISR),
	REG_STRUCT_INIT(AR5K_PISR),
	REG_STRUCT_INIT(AR5K_SISR0),
	REG_STRUCT_INIT(AR5K_SISR1),
	REG_STRUCT_INIT(AR5K_SISR2),
	REG_STRUCT_INIT(AR5K_SISR3),
	REG_STRUCT_INIT(AR5K_SISR4),
	REG_STRUCT_INIT(AR5K_IMR),
	REG_STRUCT_INIT(AR5K_PIMR),
	REG_STRUCT_INIT(AR5K_SIMR0),
	REG_STRUCT_INIT(AR5K_SIMR1),
	REG_STRUCT_INIT(AR5K_SIMR2),
	REG_STRUCT_INIT(AR5K_SIMR3),
	REG_STRUCT_INIT(AR5K_SIMR4),
	REG_STRUCT_INIT(AR5K_DCM_ADDR),
	REG_STRUCT_INIT(AR5K_DCCFG),
	REG_STRUCT_INIT(AR5K_CCFG),
	REG_STRUCT_INIT(AR5K_CPC0),
	REG_STRUCT_INIT(AR5K_CPC1),
	REG_STRUCT_INIT(AR5K_CPC2),
	REG_STRUCT_INIT(AR5K_CPC3),
	REG_STRUCT_INIT(AR5K_CPCOVF),
	REG_STRUCT_INIT(AR5K_RESET_CTL),
	REG_STRUCT_INIT(AR5K_SLEEP_CTL),
	REG_STRUCT_INIT(AR5K_INTPEND),
	REG_STRUCT_INIT(AR5K_SFR),
	REG_STRUCT_INIT(AR5K_PCICFG),
	REG_STRUCT_INIT(AR5K_GPIOCR),
	REG_STRUCT_INIT(AR5K_GPIODO),
	REG_STRUCT_INIT(AR5K_SREV),
};

static void *reg_start(struct seq_file *seq, loff_t *pos)
{
	return *pos < ARRAY_SIZE(regs) ? &regs[*pos] : NULL;
}

static void reg_stop(struct seq_file *seq, void *p)
{
	/* nothing to do */
}

static void *reg_next(struct seq_file *seq, void *p, loff_t *pos)
{
	++*pos;
	return *pos < ARRAY_SIZE(regs) ? &regs[*pos] : NULL;
}

static int reg_show(struct seq_file *seq, void *p)
{
	struct ath5k_softc *sc = seq->private;
	struct reg *r = p;
	seq_printf(seq, "%-25s0x%08x\n", r->name,
		ath5k_hw_reg_read(sc->ah, r->addr));
	return 0;
}

static struct seq_operations register_seq_ops = {
	.start = reg_start,
	.next  = reg_next,
	.stop  = reg_stop,
	.show  = reg_show
};

static int open_file_registers(struct inode *inode, struct file *file)
{
	struct seq_file *s;
	int res;
	res = seq_open(file, &register_seq_ops);
	if (res == 0) {
		s = file->private_data;
		s->private = inode->i_private;
	}
	return res;
}

static const struct file_operations fops_registers = {
	.open = open_file_registers,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
	.owner = THIS_MODULE,
};


/* debugfs: TSF */

static ssize_t read_file_tsf(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath5k_softc *sc = file->private_data;
	char buf[100];
	snprintf(buf, sizeof(buf), "0x%016llx\n",
		 (unsigned long long)ath5k_hw_get_tsf64(sc->ah));
	return simple_read_from_buffer(user_buf, count, ppos, buf, 19);
}

static ssize_t write_file_tsf(struct file *file,
				 const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct ath5k_softc *sc = file->private_data;
	char buf[20];

	if (copy_from_user(buf, userbuf, min(count, sizeof(buf))))
		return -EFAULT;

	if (strncmp(buf, "reset", 5) == 0) {
		ath5k_hw_reset_tsf(sc->ah);
		printk(KERN_INFO "debugfs reset TSF\n");
	}
	return count;
}

static const struct file_operations fops_tsf = {
	.read = read_file_tsf,
	.write = write_file_tsf,
	.open = ath5k_debugfs_open,
	.owner = THIS_MODULE,
};


/* debugfs: beacons */

static ssize_t read_file_beacon(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath5k_softc *sc = file->private_data;
	struct ath5k_hw *ah = sc->ah;
	char buf[500];
	unsigned int len = 0;
	unsigned int v;
	u64 tsf;

	v = ath5k_hw_reg_read(sc->ah, AR5K_BEACON);
	len += snprintf(buf+len, sizeof(buf)-len,
		"%-24s0x%08x\tintval: %d\tTIM: 0x%x\n",
		"AR5K_BEACON", v, v & AR5K_BEACON_PERIOD,
		(v & AR5K_BEACON_TIM) >> AR5K_BEACON_TIM_S);

	len += snprintf(buf+len, sizeof(buf)-len, "%-24s0x%08x\n",
		"AR5K_LAST_TSTP", ath5k_hw_reg_read(sc->ah, AR5K_LAST_TSTP));

	len += snprintf(buf+len, sizeof(buf)-len, "%-24s0x%08x\n\n",
		"AR5K_BEACON_CNT", ath5k_hw_reg_read(sc->ah, AR5K_BEACON_CNT));

	v = ath5k_hw_reg_read(sc->ah, AR5K_TIMER0);
	len += snprintf(buf+len, sizeof(buf)-len, "%-24s0x%08x\tTU: %08x\n",
		"AR5K_TIMER0 (TBTT)", v, v);

	v = ath5k_hw_reg_read(sc->ah, AR5K_TIMER1);
	len += snprintf(buf+len, sizeof(buf)-len, "%-24s0x%08x\tTU: %08x\n",
		"AR5K_TIMER1 (DMA)", v, v >> 3);

	v = ath5k_hw_reg_read(sc->ah, AR5K_TIMER2);
	len += snprintf(buf+len, sizeof(buf)-len, "%-24s0x%08x\tTU: %08x\n",
		"AR5K_TIMER2 (SWBA)", v, v >> 3);

	v = ath5k_hw_reg_read(sc->ah, AR5K_TIMER3);
	len += snprintf(buf+len, sizeof(buf)-len, "%-24s0x%08x\tTU: %08x\n",
		"AR5K_TIMER3 (ATIM)", v, v);

	tsf = ath5k_hw_get_tsf64(sc->ah);
	len += snprintf(buf+len, sizeof(buf)-len,
		"TSF\t\t0x%016llx\tTU: %08x\n",
		(unsigned long long)tsf, TSF_TO_TU(tsf));

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_beacon(struct file *file,
				 const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct ath5k_softc *sc = file->private_data;
	struct ath5k_hw *ah = sc->ah;
	char buf[20];

	if (copy_from_user(buf, userbuf, min(count, sizeof(buf))))
		return -EFAULT;

	if (strncmp(buf, "disable", 7) == 0) {
		AR5K_REG_DISABLE_BITS(ah, AR5K_BEACON, AR5K_BEACON_ENABLE);
		printk(KERN_INFO "debugfs disable beacons\n");
	} else if (strncmp(buf, "enable", 6) == 0) {
		AR5K_REG_ENABLE_BITS(ah, AR5K_BEACON, AR5K_BEACON_ENABLE);
		printk(KERN_INFO "debugfs enable beacons\n");
	}
	return count;
}

static const struct file_operations fops_beacon = {
	.read = read_file_beacon,
	.write = write_file_beacon,
	.open = ath5k_debugfs_open,
	.owner = THIS_MODULE,
};


/* debugfs: reset */

static ssize_t write_file_reset(struct file *file,
				 const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct ath5k_softc *sc = file->private_data;
	tasklet_schedule(&sc->restq);
	return count;
}

static const struct file_operations fops_reset = {
	.write = write_file_reset,
	.open = ath5k_debugfs_open,
	.owner = THIS_MODULE,
};


/* debugfs: debug level */

static struct {
	enum ath5k_debug_level level;
	const char *name;
	const char *desc;
} dbg_info[] = {
	{ ATH5K_DEBUG_RESET,	"reset",	"reset and initialization" },
	{ ATH5K_DEBUG_INTR,	"intr",		"interrupt handling" },
	{ ATH5K_DEBUG_MODE,	"mode",		"mode init/setup" },
	{ ATH5K_DEBUG_XMIT,	"xmit",		"basic xmit operation" },
	{ ATH5K_DEBUG_BEACON,	"beacon",	"beacon handling" },
	{ ATH5K_DEBUG_CALIBRATE, "calib",	"periodic calibration" },
	{ ATH5K_DEBUG_TXPOWER,	"txpower",	"transmit power setting" },
	{ ATH5K_DEBUG_LED,	"led",		"LED mamagement" },
	{ ATH5K_DEBUG_DUMP_RX,	"dumprx",	"print received skb content" },
	{ ATH5K_DEBUG_DUMP_TX,	"dumptx",	"print transmit skb content" },
	{ ATH5K_DEBUG_DUMPBANDS, "dumpbands",	"dump bands" },
	{ ATH5K_DEBUG_TRACE,	"trace",	"trace function calls" },
	{ ATH5K_DEBUG_ANY,	"all",		"show all debug levels" },
};

static ssize_t read_file_debug(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath5k_softc *sc = file->private_data;
	char buf[700];
	unsigned int len = 0;
	unsigned int i;

	len += snprintf(buf+len, sizeof(buf)-len,
		"DEBUG LEVEL: 0x%08x\n\n", sc->debug.level);

	for (i = 0; i < ARRAY_SIZE(dbg_info) - 1; i++) {
		len += snprintf(buf+len, sizeof(buf)-len,
			"%10s %c 0x%08x - %s\n", dbg_info[i].name,
			sc->debug.level & dbg_info[i].level ? '+' : ' ',
			dbg_info[i].level, dbg_info[i].desc);
	}
	len += snprintf(buf+len, sizeof(buf)-len,
		"%10s %c 0x%08x - %s\n", dbg_info[i].name,
		sc->debug.level == dbg_info[i].level ? '+' : ' ',
		dbg_info[i].level, dbg_info[i].desc);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_file_debug(struct file *file,
				 const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct ath5k_softc *sc = file->private_data;
	unsigned int i;
	char buf[20];

	if (copy_from_user(buf, userbuf, min(count, sizeof(buf))))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(dbg_info); i++) {
		if (strncmp(buf, dbg_info[i].name,
					strlen(dbg_info[i].name)) == 0) {
			sc->debug.level ^= dbg_info[i].level; /* toggle bit */
			break;
		}
	}
	return count;
}

static const struct file_operations fops_debug = {
	.read = read_file_debug,
	.write = write_file_debug,
	.open = ath5k_debugfs_open,
	.owner = THIS_MODULE,
};


/* init */

void
ath5k_debug_init(void)
{
	ath5k_global_debugfs = debugfs_create_dir("ath5k", NULL);
}

void
ath5k_debug_init_device(struct ath5k_softc *sc)
{
	sc->debug.level = ath5k_debug;

	sc->debug.debugfs_phydir = debugfs_create_dir(wiphy_name(sc->hw->wiphy),
				ath5k_global_debugfs);

	sc->debug.debugfs_debug = debugfs_create_file("debug", 0666,
				sc->debug.debugfs_phydir, sc, &fops_debug);

	sc->debug.debugfs_registers = debugfs_create_file("registers", 0444,
				sc->debug.debugfs_phydir, sc, &fops_registers);

	sc->debug.debugfs_tsf = debugfs_create_file("tsf", 0666,
				sc->debug.debugfs_phydir, sc, &fops_tsf);

	sc->debug.debugfs_beacon = debugfs_create_file("beacon", 0666,
				sc->debug.debugfs_phydir, sc, &fops_beacon);

	sc->debug.debugfs_reset = debugfs_create_file("reset", 0222,
				sc->debug.debugfs_phydir, sc, &fops_reset);
}

void
ath5k_debug_finish(void)
{
	debugfs_remove(ath5k_global_debugfs);
}

void
ath5k_debug_finish_device(struct ath5k_softc *sc)
{
	debugfs_remove(sc->debug.debugfs_debug);
	debugfs_remove(sc->debug.debugfs_registers);
	debugfs_remove(sc->debug.debugfs_tsf);
	debugfs_remove(sc->debug.debugfs_beacon);
	debugfs_remove(sc->debug.debugfs_reset);
	debugfs_remove(sc->debug.debugfs_phydir);
}


/* functions used in other places */

void
ath5k_debug_dump_bands(struct ath5k_softc *sc)
{
	unsigned int b, i;

	if (likely(!(sc->debug.level & ATH5K_DEBUG_DUMPBANDS)))
		return;

	BUG_ON(!sc->sbands);

	for (b = 0; b < IEEE80211_NUM_BANDS; b++) {
		struct ieee80211_supported_band *band = &sc->sbands[b];
		char bname[5];
		switch (band->band) {
		case IEEE80211_BAND_2GHZ:
			strcpy(bname, "2 GHz");
			break;
		case IEEE80211_BAND_5GHZ:
			strcpy(bname, "5 GHz");
			break;
		default:
			printk(KERN_DEBUG "Band not supported: %d\n",
				band->band);
			return;
		}
		printk(KERN_DEBUG "Band %s: channels %d, rates %d\n", bname,
				band->n_channels, band->n_bitrates);
		printk(KERN_DEBUG " channels:\n");
		for (i = 0; i < band->n_channels; i++)
			printk(KERN_DEBUG "  %3d %d %.4x %.4x\n",
					ieee80211_frequency_to_channel(
						band->channels[i].center_freq),
					band->channels[i].center_freq,
					band->channels[i].hw_value,
					band->channels[i].flags);
		printk(KERN_DEBUG " rates:\n");
		for (i = 0; i < band->n_bitrates; i++)
			printk(KERN_DEBUG "  %4d %.4x %.4x %.4x\n",
					band->bitrates[i].bitrate,
					band->bitrates[i].hw_value,
					band->bitrates[i].flags,
					band->bitrates[i].hw_value_short);
	}
}

static inline void
ath5k_debug_printrxbuf(struct ath5k_buf *bf, int done,
		       struct ath5k_rx_status *rs)
{
	struct ath5k_desc *ds = bf->desc;
	struct ath5k_hw_all_rx_desc *rd = &ds->ud.ds_rx;

	printk(KERN_DEBUG "R (%p %llx) %08x %08x %08x %08x %08x %08x %c\n",
		ds, (unsigned long long)bf->daddr,
		ds->ds_link, ds->ds_data,
		rd->rx_ctl.rx_control_0, rd->rx_ctl.rx_control_1,
		rd->u.rx_stat.rx_status_0, rd->u.rx_stat.rx_status_0,
		!done ? ' ' : (rs->rs_status == 0) ? '*' : '!');
}

void
ath5k_debug_printrxbuffs(struct ath5k_softc *sc, struct ath5k_hw *ah)
{
	struct ath5k_desc *ds;
	struct ath5k_buf *bf;
	struct ath5k_rx_status rs = {};
	int status;

	if (likely(!(sc->debug.level & ATH5K_DEBUG_RESET)))
		return;

	printk(KERN_DEBUG "rx queue %x, link %p\n",
		ath5k_hw_get_rx_buf(ah), sc->rxlink);

	spin_lock_bh(&sc->rxbuflock);
	list_for_each_entry(bf, &sc->rxbuf, list) {
		ds = bf->desc;
		status = ah->ah_proc_rx_desc(ah, ds, &rs);
		if (!status)
			ath5k_debug_printrxbuf(bf, status == 0, &rs);
	}
	spin_unlock_bh(&sc->rxbuflock);
}

void
ath5k_debug_dump_skb(struct ath5k_softc *sc,
			struct sk_buff *skb, const char *prefix, int tx)
{
	char buf[16];

	if (likely(!((tx && (sc->debug.level & ATH5K_DEBUG_DUMP_TX)) ||
		     (!tx && (sc->debug.level & ATH5K_DEBUG_DUMP_RX)))))
		return;

	snprintf(buf, sizeof(buf), "%s %s", wiphy_name(sc->hw->wiphy), prefix);

	print_hex_dump_bytes(buf, DUMP_PREFIX_NONE, skb->data,
		min(200U, skb->len));

	printk(KERN_DEBUG "\n");
}

void
ath5k_debug_printtxbuf(struct ath5k_softc *sc, struct ath5k_buf *bf)
{
	struct ath5k_desc *ds = bf->desc;
	struct ath5k_hw_5212_tx_desc *td = &ds->ud.ds_tx5212;
	struct ath5k_tx_status ts = {};
	int done;

	if (likely(!(sc->debug.level & ATH5K_DEBUG_RESET)))
		return;

	done = sc->ah->ah_proc_tx_desc(sc->ah, bf->desc, &ts);

	printk(KERN_DEBUG "T (%p %llx) %08x %08x %08x %08x %08x %08x %08x "
		"%08x %c\n", ds, (unsigned long long)bf->daddr, ds->ds_link,
		ds->ds_data, td->tx_ctl.tx_control_0, td->tx_ctl.tx_control_1,
		td->tx_ctl.tx_control_2, td->tx_ctl.tx_control_3,
		td->tx_stat.tx_status_0, td->tx_stat.tx_status_1,
		done ? ' ' : (ts.ts_status == 0) ? '*' : '!');
}

#endif /* ifdef CONFIG_ATH5K_DEBUG */
