/******************************************************************************
 *
 * Name:	skproc.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.11 $
 * Date:	$Date: 2003/12/11 16:03:57 $
 * Purpose:	Funktions to display statictic data
 *
 ******************************************************************************/
 
/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect GmbH.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Created 22-Nov-2000
 *	Author: Mirko Lindner (mlindner@syskonnect.de)
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"
#include "h/skversion.h"

static int sk_seq_show(struct seq_file *seq, void *v);
static int sk_proc_open(struct inode *inode, struct file *file);

struct file_operations sk_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= sk_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


/*****************************************************************************
 *
 *      sk_seq_show - show proc information of a particular adapter
 *
 * Description:
 *  This function fills the proc entry with statistic data about 
 *  the ethernet device. It invokes the generic sk_gen_browse() to
 *  print out all items one per one.
 *  
 * Returns: 0
 *      
 */
static int sk_seq_show(struct seq_file *seq, void *v)
{
	struct net_device *dev = seq->private;
	DEV_NET			*pNet = netdev_priv(dev);
	SK_AC			*pAC = pNet->pAC;
	SK_PNMI_STRUCT_DATA 	*pPnmiStruct = &pAC->PnmiStruct;
	unsigned long		Flags;	
	unsigned int		Size;
	char			sens_msg[50];
	int 			t;
	int 			i;

	/* NetIndex in GetStruct is now required, zero is only dummy */
	for (t=pAC->GIni.GIMacsFound; t > 0; t--) {
		if ((pAC->GIni.GIMacsFound == 2) && pAC->RlmtNets == 1)
			t--;

		spin_lock_irqsave(&pAC->SlowPathLock, Flags);
		Size = SK_PNMI_STRUCT_SIZE;
#ifdef SK_DIAG_SUPPORT
		if (pAC->BoardLevel == SK_INIT_DATA) {
			SK_MEMCPY(&(pAC->PnmiStruct), &(pAC->PnmiBackup), sizeof(SK_PNMI_STRUCT_DATA));
			if (pAC->DiagModeActive == DIAG_NOTACTIVE) {
				pAC->Pnmi.DiagAttached = SK_DIAG_IDLE;
			}
		} else {
			SkPnmiGetStruct(pAC, pAC->IoBase, pPnmiStruct, &Size, t-1);
		}
#else
		SkPnmiGetStruct(pAC, pAC->IoBase, 
				pPnmiStruct, &Size, t-1);
#endif
		spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	
		if (pAC->dev[t-1] == dev) {
			SK_PNMI_STAT	*pPnmiStat = &pPnmiStruct->Stat[0];

			seq_printf(seq, "\nDetailed statistic for device %s\n",
				      pAC->dev[t-1]->name);
			seq_printf(seq, "=======================================\n");
	
			/* Board statistics */
			seq_printf(seq, "\nBoard statistics\n\n");
			seq_printf(seq, "Active Port                    %c\n",
				      'A' + pAC->Rlmt.Net[t-1].Port[pAC->Rlmt.
								    Net[t-1].PrefPort]->PortNumber);
			seq_printf(seq, "Preferred Port                 %c\n",
				      'A' + pAC->Rlmt.Net[t-1].Port[pAC->Rlmt.
								    Net[t-1].PrefPort]->PortNumber);

			seq_printf(seq, "Bus speed (MHz)                %d\n",
				      pPnmiStruct->BusSpeed);

			seq_printf(seq, "Bus width (Bit)                %d\n",
				      pPnmiStruct->BusWidth);
			seq_printf(seq, "Driver version                 %s\n",
				      VER_STRING);
			seq_printf(seq, "Hardware revision              v%d.%d\n",
				      (pAC->GIni.GIPciHwRev >> 4) & 0x0F,
				      pAC->GIni.GIPciHwRev & 0x0F);

			/* Print sensor informations */
			for (i=0; i < pAC->I2c.MaxSens; i ++) {
				/* Check type */
				switch (pAC->I2c.SenTable[i].SenType) {
				case 1:
					strcpy(sens_msg, pAC->I2c.SenTable[i].SenDesc);
					strcat(sens_msg, " (C)");
					seq_printf(seq, "%-25s      %d.%02d\n",
						      sens_msg,
						      pAC->I2c.SenTable[i].SenValue / 10,
						      pAC->I2c.SenTable[i].SenValue % 10);

					strcpy(sens_msg, pAC->I2c.SenTable[i].SenDesc);
					strcat(sens_msg, " (F)");
					seq_printf(seq, "%-25s      %d.%02d\n",
						      sens_msg,
						      ((((pAC->I2c.SenTable[i].SenValue)
							 *10)*9)/5 + 3200)/100,
						      ((((pAC->I2c.SenTable[i].SenValue)
							 *10)*9)/5 + 3200) % 10);
					break;
				case 2:
					strcpy(sens_msg, pAC->I2c.SenTable[i].SenDesc);
					strcat(sens_msg, " (V)");
					seq_printf(seq, "%-25s      %d.%03d\n",
						      sens_msg,
						      pAC->I2c.SenTable[i].SenValue / 1000,
						      pAC->I2c.SenTable[i].SenValue % 1000);
					break;
				case 3:
					strcpy(sens_msg, pAC->I2c.SenTable[i].SenDesc);
					strcat(sens_msg, " (rpm)");
					seq_printf(seq, "%-25s      %d\n",
						      sens_msg,
						      pAC->I2c.SenTable[i].SenValue);
					break;
				default:
					break;
				}
			}
				
			/*Receive statistics */
			seq_printf(seq, "\nReceive statistics\n\n");

			seq_printf(seq, "Received bytes                 %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxOctetsOkCts);
			seq_printf(seq, "Received packets               %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxOkCts);
#if 0
			if (pAC->GIni.GP[0].PhyType == SK_PHY_XMAC && 
			    pAC->HWRevision < 12) {
				pPnmiStruct->InErrorsCts = pPnmiStruct->InErrorsCts - 
					pPnmiStat->StatRxShortsCts;
				pPnmiStat->StatRxShortsCts = 0;
			}
#endif
			if (dev->mtu > 1500)
				pPnmiStruct->InErrorsCts = pPnmiStruct->InErrorsCts -
					pPnmiStat->StatRxTooLongCts;

			seq_printf(seq, "Receive errors                 %Lu\n",
				      (unsigned long long) pPnmiStruct->InErrorsCts);
			seq_printf(seq, "Receive dropped                %Lu\n",
				      (unsigned long long) pPnmiStruct->RxNoBufCts);
			seq_printf(seq, "Received multicast             %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxMulticastOkCts);
			seq_printf(seq, "Receive error types\n");
			seq_printf(seq, "   length                      %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxRuntCts);
			seq_printf(seq, "   buffer overflow             %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxFifoOverflowCts);
			seq_printf(seq, "   bad crc                     %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxFcsCts);
			seq_printf(seq, "   framing                     %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxFramingCts);
			seq_printf(seq, "   missed frames               %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxMissedCts);

			if (dev->mtu > 1500)
				pPnmiStat->StatRxTooLongCts = 0;

			seq_printf(seq, "   too long                    %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxTooLongCts);					
			seq_printf(seq, "   carrier extension           %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxCextCts);				
			seq_printf(seq, "   too short                   %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxShortsCts);				
			seq_printf(seq, "   symbol                      %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxSymbolCts);				
			seq_printf(seq, "   LLC MAC size                %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxIRLengthCts);				
			seq_printf(seq, "   carrier event               %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxCarrierCts);				
			seq_printf(seq, "   jabber                      %Lu\n",
				      (unsigned long long) pPnmiStat->StatRxJabberCts);				


			/*Transmit statistics */
			seq_printf(seq, "\nTransmit statistics\n\n");
				
			seq_printf(seq, "Transmited bytes               %Lu\n",
				      (unsigned long long) pPnmiStat->StatTxOctetsOkCts);
			seq_printf(seq, "Transmited packets             %Lu\n",
				      (unsigned long long) pPnmiStat->StatTxOkCts);
			seq_printf(seq, "Transmit errors                %Lu\n",
				      (unsigned long long) pPnmiStat->StatTxSingleCollisionCts);
			seq_printf(seq, "Transmit dropped               %Lu\n",
				      (unsigned long long) pPnmiStruct->TxNoBufCts);
			seq_printf(seq, "Transmit collisions            %Lu\n",
				      (unsigned long long) pPnmiStat->StatTxSingleCollisionCts);
			seq_printf(seq, "Transmit error types\n");
			seq_printf(seq, "   excessive collision         %ld\n",
				      pAC->stats.tx_aborted_errors);
			seq_printf(seq, "   carrier                     %Lu\n",
				      (unsigned long long) pPnmiStat->StatTxCarrierCts);
			seq_printf(seq, "   fifo underrun               %Lu\n",
				      (unsigned long long) pPnmiStat->StatTxFifoUnderrunCts);
			seq_printf(seq, "   heartbeat                   %Lu\n",
				      (unsigned long long) pPnmiStat->StatTxCarrierCts);
			seq_printf(seq, "   window                      %ld\n",
				      pAC->stats.tx_window_errors);
				
		}
	}
	return 0;
}

/*****************************************************************************
 *
 *      sk_proc_open - register the show function when proc is open'ed
 *  
 * Description:
 *  This function is called whenever a sk98lin proc file is queried.
 *  
 * Returns: the return value of single_open()
 *      
 */
static int sk_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, sk_seq_show, PDE(inode)->data);
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
