/*
 * ft1000_proc.c - ft1000 proc interface
 *
 * Copyright	(C) 2009-2010 Quintec
 *		(C) 2010 Open-nandra
 *      <marek.belisko@open-nandra.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>


#include "ft1000_usb.h"

#define FT1000_PROC_DIR "ft1000"


#define PUTM_TO_PAGE(len,page,args...) \
	len += snprintf(page+len, PAGE_SIZE - len, args)

#define PUTX_TO_PAGE(len,page,message,size,var) \
	len += snprintf(page+len, PAGE_SIZE - len, message); \
	for (i = 0; i < (size - 1); i++) {\
		len += snprintf(page+len, PAGE_SIZE - len, "%02x:", var[i]); \
	} \
	len += snprintf(page+len, PAGE_SIZE - len, "%02x\n", var[i])

#define PUTD_TO_PAGE(len,page,message,size,var) \
	len += snprintf(page+len, PAGE_SIZE - len, message); \
	for (i = 0; i < (size - 1); i++) {\
		len += snprintf(page+len, PAGE_SIZE - len, "%d.", var[i]); \
	} \
	len += snprintf(page+len, PAGE_SIZE - len, "%d\n", var[i])


#define FTNET_PROC init_net.proc_net


int ft1000_read_dpram16 (struct ft1000_device *ft1000dev, u16 indx,
			 u8 *buffer, u8 highlow);


static int
ft1000ReadProc(char *page, char **start, off_t off, int count, int *eof,
		void *data)
{
	struct net_device *dev;
	int len;
	int i;
	unsigned short ledStat;
	unsigned short conStat;

	struct ft1000_info *info;

	char *status[] = { 
		"Idle (Disconnect)", 
		"Searching",
		"Active (Connected)",
		"Waiting for L2",
		"Sleep",
		"No Coverage",
		"",
		"",
	};

	char *signal[] = { "", "*", "**", "***", "****" };
	int strength;
	int quality;
	struct timeval tv;
	time_t delta;

	dev = (struct net_device *) data;
	info = netdev_priv(dev);

	if (off > 0) {
		*eof = 1;
		return 0;
	}


	if (info->ProgConStat != 0xFF) {
		ft1000_read_dpram16(info->pFt1000Dev, FT1000_MAG_DSP_LED,
			   (u8 *)&ledStat, FT1000_MAG_DSP_LED_INDX);
		info->LedStat = ntohs(ledStat);

		ft1000_read_dpram16(info->pFt1000Dev, FT1000_MAG_DSP_CON_STATE,
			(u8 *)&conStat, FT1000_MAG_DSP_CON_STATE_INDX);
		info->ConStat = ntohs(conStat);
		do_gettimeofday(&tv);
		delta = (tv.tv_sec - info->ConTm);
	} else {
		info->ConStat = 0xf;
		delta = 0;
	}

	i = (info->LedStat) & 0xf;
	switch (i) {
	case 0x1:
		strength = 1;
		break;
	case 0x3:
		strength = 2;
		break;
	case 0x7:
		strength = 3;
		break;
	case 0xf:
		strength = 4;
		break;
	default:
		strength = 0;
	}

	i = (info->LedStat >> 8) & 0xf;
	switch (i) {
	case 0x1:
		quality = 1;
		break;
	case 0x3:
		quality = 2;
		break;
	case 0x7:
		quality = 3;
		break;
	case 0xf:
		quality = 4;
		break;
	default:
		quality = 0;
	}

	len = 0;
	PUTM_TO_PAGE(len, page, "Connection Time: %02ld:%02ld:%02ld\n",
      		((delta / 3600) % 24), ((delta / 60) % 60), (delta % 60));
	PUTM_TO_PAGE(len, page, "Connection Time[s]: %ld\n", delta);
	PUTM_TO_PAGE(len, page, "Asic ID: %s\n",
      	(info->AsicID) ==
      	ELECTRABUZZ_ID ? "ELECTRABUZZ ASIC" : "MAGNEMITE ASIC");
	PUTX_TO_PAGE(len, page, "SKU: ", SKUSZ, info->Sku);
	PUTX_TO_PAGE(len, page, "EUI64: ", EUISZ, info->eui64);
	PUTD_TO_PAGE(len, page, "DSP version number: ", DSPVERSZ, info->DspVer);
	PUTX_TO_PAGE(len, page, "Hardware Serial Number: ", HWSERNUMSZ,
      		info->HwSerNum);
	PUTX_TO_PAGE(len, page, "Caliberation Version: ", CALVERSZ,
      		info->RfCalVer);
	PUTD_TO_PAGE(len, page, "Caliberation Date: ", CALDATESZ,
		info->RfCalDate);
	PUTM_TO_PAGE(len, page, "Media State: %s\n",
      		(info->mediastate) ? "link" : "no link");
	PUTM_TO_PAGE(len, page, "Connection Status: %s\n",
      		status[((info->ConStat) & 0x7)]);
	PUTM_TO_PAGE(len, page, "RX packets: %ld\n", info->stats.rx_packets);
	PUTM_TO_PAGE(len, page, "TX packets: %ld\n", info->stats.tx_packets);
	PUTM_TO_PAGE(len, page, "RX bytes: %ld\n", info->stats.rx_bytes);
	PUTM_TO_PAGE(len, page, "TX bytes: %ld\n", info->stats.tx_bytes);
	PUTM_TO_PAGE(len, page, "Signal Strength: %s\n", signal[strength]);
	PUTM_TO_PAGE(len, page, "Signal Quality: %s\n", signal[quality]);

	return len;
}

static int
ft1000NotifyProc(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct ft1000_info *info;
	struct proc_dir_entry *ft1000_proc_file;

	info = netdev_priv(dev);

	switch (event) {
	case NETDEV_CHANGENAME:
		remove_proc_entry(info->netdevname, info->ft1000_proc_dir);
		ft1000_proc_file = create_proc_read_entry(dev->name, 0644,
					info->ft1000_proc_dir,
					ft1000ReadProc, dev);
		snprintf(info->netdevname, IFNAMSIZ, "%s", dev->name);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block ft1000_netdev_notifier = {
	.notifier_call = ft1000NotifyProc,
};


int ft1000_init_proc(struct net_device *dev)
{
	struct ft1000_info *info;
	struct proc_dir_entry *ft1000_proc_file;
	int ret = 0;

	info = netdev_priv(dev);

	info->ft1000_proc_dir = proc_mkdir(FT1000_PROC_DIR, FTNET_PROC);
	if (info->ft1000_proc_dir == NULL) {
		printk(KERN_WARNING "Unable to create %s dir.\n",
			FT1000_PROC_DIR);
		ret = -EINVAL;
		goto fail;
	}

	ft1000_proc_file =
		create_proc_read_entry(dev->name, 0644,
			info->ft1000_proc_dir, ft1000ReadProc, dev);

	if (ft1000_proc_file == NULL) {
		printk(KERN_WARNING "Unable to create /proc entry.\n");
		ret = -EINVAL;
		goto fail_entry;
	}

	snprintf(info->netdevname, IFNAMSIZ, "%s", dev->name);

	ret = register_netdevice_notifier(&ft1000_netdev_notifier);
	if (ret)
		goto fail_notif;

	return 0;

fail_notif:
	remove_proc_entry(info->netdevname, info->ft1000_proc_dir);
fail_entry:
	remove_proc_entry(FT1000_PROC_DIR, FTNET_PROC);
fail:
	return ret;
}

void ft1000_cleanup_proc(struct ft1000_info *info)
{
	remove_proc_entry(info->netdevname, info->ft1000_proc_dir);
	remove_proc_entry(FT1000_PROC_DIR, FTNET_PROC);
	unregister_netdevice_notifier(&ft1000_netdev_notifier);
}
