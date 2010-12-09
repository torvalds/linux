//---------------------------------------------------------------------------
// FT1000 driver for Flarion Flash OFDM NIC Device
//
// Copyright (C) 2006 Flarion Technologies, All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option) any
// later version. This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details. You should have received a copy of the GNU General Public
// License along with this program; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place -
// Suite 330, Boston, MA 02111-1307, USA.
//---------------------------------------------------------------------------
//
// File:         ft1000_chdev.c
//
// Description:  Custom character device dispatch routines.
//
// History:
// 8/29/02    Whc                Ported to Linux.
// 6/05/06    Whc                Porting to Linux 2.6.9
//
//---------------------------------------------------------------------------
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/netdevice.h>
#include <linux/delay.h>

#include <linux/fs.h>
#include <linux/kmod.h>
#include <linux/ioctl.h>
#include <linux/unistd.h>
#include <linux/debugfs.h>
#include "ft1000_usb.h"
//#include "ft1000_ioctl.h"

static int ft1000_flarion_cnt = 0;

//need to looking usage of ft1000Handle

static int ft1000_ChOpen (struct inode *Inode, struct file *File);
static unsigned int ft1000_ChPoll(struct file *file, poll_table *wait);
static long ft1000_ChIoctl(struct file *File, unsigned int Command,
                           unsigned long Argument);
static int ft1000_ChRelease (struct inode *Inode, struct file *File);

// Global pointer to device object
static struct ft1000_device *pdevobj[MAX_NUM_CARDS + 2];
//static devfs_handle_t ft1000Handle[MAX_NUM_CARDS];

// List to free receive command buffer pool
struct list_head freercvpool;

// lock to arbitrate free buffer list for receive command data
spinlock_t free_buff_lock;

int numofmsgbuf = 0;

// Global variable to indicate that all provisioning data is sent to DSP
//bool fProvComplete;

//
// Table of entry-point routines for char device
//
static struct file_operations ft1000fops =
{
	.unlocked_ioctl	= ft1000_ChIoctl,
	.poll		= ft1000_ChPoll,
	.open		= ft1000_ChOpen,
	.release	= ft1000_ChRelease,
	.llseek		= no_llseek,
};

//---------------------------------------------------------------------------
// Function:    ft1000_get_buffer
//
// Parameters:
//
// Returns:
//
// Description:
//
// Notes:
//
//---------------------------------------------------------------------------
struct dpram_blk *ft1000_get_buffer(struct list_head *bufflist)
{
    unsigned long flags;
	struct dpram_blk *ptr;

    spin_lock_irqsave(&free_buff_lock, flags);
    // Check if buffer is available
    if ( list_empty(bufflist) ) {
        DEBUG("ft1000_get_buffer:  No more buffer - %d\n", numofmsgbuf);
        ptr = NULL;
    }
    else {
        numofmsgbuf--;
	ptr = list_entry(bufflist->next, struct dpram_blk, list);
        list_del(&ptr->list);
        //DEBUG("ft1000_get_buffer: number of free msg buffers = %d\n", numofmsgbuf);
    }
    spin_unlock_irqrestore(&free_buff_lock, flags);

    return ptr;
}




//---------------------------------------------------------------------------
// Function:    ft1000_free_buffer
//
// Parameters:
//
// Returns:
//
// Description:
//
// Notes:
//
//---------------------------------------------------------------------------
void ft1000_free_buffer(struct dpram_blk *pdpram_blk, struct list_head *plist)
{
    unsigned long flags;

    spin_lock_irqsave(&free_buff_lock, flags);
    // Put memory back to list
    list_add_tail(&pdpram_blk->list, plist);
    numofmsgbuf++;
    //DEBUG("ft1000_free_buffer: number of free msg buffers = %d\n", numofmsgbuf);
    spin_unlock_irqrestore(&free_buff_lock, flags);
}

//---------------------------------------------------------------------------
// Function:    ft1000_CreateDevice
//
// Parameters:  dev - pointer to adapter object
//
// Returns:     0 if successful
//
// Description: Creates a private char device.
//
// Notes:       Only called by init_module().
//
//---------------------------------------------------------------------------
int ft1000_CreateDevice(struct ft1000_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev->net);
    int result;
    int i;
	struct dentry *dir, *file;
	struct ft1000_debug_dirs *tmp;

    // make a new device name
    sprintf(info->DeviceName, "%s%d", "FT1000_", info->CardNumber);

    DEBUG("ft1000_CreateDevice: number of instance = %d\n", ft1000_flarion_cnt);
    DEBUG("DeviceCreated = %x\n", info->DeviceCreated);

    //save the device info to global array
    pdevobj[info->CardNumber] = dev;

    DEBUG("ft1000_CreateDevice: ******SAVED pdevobj[%d]=%p\n", info->CardNumber, pdevobj[info->CardNumber]);	//aelias [+] reason:up

    if (info->DeviceCreated)
    {
	DEBUG("ft1000_CreateDevice: \"%s\" already registered\n", info->DeviceName);
	return -EIO;
    }


    // register the device
    DEBUG("ft1000_CreateDevice: \"%s\" device registration\n", info->DeviceName);
    info->DeviceMajor = 0;

	tmp = kmalloc(sizeof(struct ft1000_debug_dirs), GFP_KERNEL);
	if (tmp == NULL) {
		result = -1;
		goto fail;
	}

	dir = debugfs_create_dir(info->DeviceName, 0);
	if (IS_ERR(dir)) {
		result = PTR_ERR(dir);
		goto debug_dir_fail;
	}

	file = debugfs_create_file("device", S_IRUGO | S_IWUGO, dir,
					NULL, &ft1000fops);
	if (IS_ERR(file)) {
		result = PTR_ERR(file);
		goto debug_file_fail;
	}

	tmp->dent = dir;
	tmp->file = file;
	tmp->int_number = info->CardNumber;
	list_add(&(tmp->list), &(info->nodes.list));

    DEBUG("ft1000_CreateDevice: registered char device \"%s\"\n", info->DeviceName);

    // initialize application information

//    if (ft1000_flarion_cnt == 0) {
//
//    	  DEBUG("Initialize free_buff_lock and freercvpool\n");
//        spin_lock_init(&free_buff_lock);
//
//        // initialize a list of buffers to be use for queuing up receive command data
//        INIT_LIST_HEAD (&freercvpool);
//
//        // create list of free buffers
//        for (i=0; i<NUM_OF_FREE_BUFFERS; i++) {
//            // Get memory for DPRAM_DATA link list
//            pdpram_blk = kmalloc ( sizeof(struct dpram_blk), GFP_KERNEL );
//            // Get a block of memory to store command data
//            pdpram_blk->pbuffer = kmalloc ( MAX_CMD_SQSIZE, GFP_KERNEL );
//            // link provisioning data
//            list_add_tail (&pdpram_blk->list, &freercvpool);
//        }
//        numofmsgbuf = NUM_OF_FREE_BUFFERS;
//    }


    // initialize application information
    info->appcnt = 0;
    for (i=0; i<MAX_NUM_APP; i++) {
        info->app_info[i].nTxMsg = 0;
        info->app_info[i].nRxMsg = 0;
        info->app_info[i].nTxMsgReject = 0;
        info->app_info[i].nRxMsgMiss = 0;
        info->app_info[i].fileobject = NULL;
        info->app_info[i].app_id = i+1;
        info->app_info[i].DspBCMsgFlag = 0;
        info->app_info[i].NumOfMsg = 0;
        init_waitqueue_head(&info->app_info[i].wait_dpram_msg);
        INIT_LIST_HEAD (&info->app_info[i].app_sqlist);
    }




//    ft1000Handle[info->CardNumber] = devfs_register(NULL, info->DeviceName, DEVFS_FL_AUTO_DEVNUM, 0, 0,
//                                  S_IFCHR | S_IRUGO | S_IWUGO, &ft1000fops, NULL);


    info->DeviceCreated = TRUE;
    ft1000_flarion_cnt++;

	return 0;

debug_file_fail:
	debugfs_remove(dir);
debug_dir_fail:
	kfree(tmp);
fail:
	return result;
}

//---------------------------------------------------------------------------
// Function:    ft1000_DestroyDeviceDEBUG
//
// Parameters:  dev - pointer to adapter object
//
// Description: Destroys a private char device.
//
// Notes:       Only called by cleanup_module().
//
//---------------------------------------------------------------------------
void ft1000_DestroyDevice(struct net_device *dev)
{
	struct ft1000_info *info = netdev_priv(dev);
		int i;
	struct dpram_blk *pdpram_blk;
	struct dpram_blk *ptr;
	struct list_head *pos, *q;
	struct ft1000_debug_dirs *dir;

    DEBUG("ft1000_chdev:ft1000_DestroyDevice called\n");



    if (info->DeviceCreated)
	{
        ft1000_flarion_cnt--;
		list_for_each_safe(pos, q, &info->nodes.list) {
			dir = list_entry(pos, struct ft1000_debug_dirs, list);
			if (dir->int_number == info->CardNumber) {
				debugfs_remove(dir->file);
				debugfs_remove(dir->dent);
				list_del(pos);
				kfree(dir);
			}
		}
		DEBUG("ft1000_DestroyDevice: unregistered device \"%s\"\n",
					   info->DeviceName);

        // Make sure we free any memory reserve for slow Queue
        for (i=0; i<MAX_NUM_APP; i++) {
            while (list_empty(&info->app_info[i].app_sqlist) == 0) {
                pdpram_blk = list_entry(info->app_info[i].app_sqlist.next, struct dpram_blk, list);
                list_del(&pdpram_blk->list);
                ft1000_free_buffer(pdpram_blk, &freercvpool);

            }
            wake_up_interruptible(&info->app_info[i].wait_dpram_msg);
        }

        // Remove buffer allocated for receive command data
        if (ft1000_flarion_cnt == 0) {
            while (list_empty(&freercvpool) == 0) {
		ptr = list_entry(freercvpool.next, struct dpram_blk, list);
                list_del(&ptr->list);
                kfree(ptr->pbuffer);
                kfree(ptr);
            }
        }

//        devfs_unregister(ft1000Handle[info->CardNumber]);

		info->DeviceCreated = FALSE;

		pdevobj[info->CardNumber] = NULL;
	}


}

//---------------------------------------------------------------------------
// Function:    ft1000_ChOpen
//
// Parameters:
//
// Description:
//
// Notes:
//
//---------------------------------------------------------------------------
static int ft1000_ChOpen (struct inode *Inode, struct file *File)
{
	struct ft1000_info *info;
    int i,num;

    DEBUG("ft1000_ChOpen called\n");
    num = (MINOR(Inode->i_rdev) & 0xf);
    DEBUG("ft1000_ChOpen: minor number=%d\n", num);

    for (i=0; i<5; i++)
        DEBUG("pdevobj[%d]=%p\n", i, pdevobj[i]); //aelias [+] reason: down

    if ( pdevobj[num] != NULL )
        //info = (struct ft1000_info *)(pdevobj[num]->net->priv);
		info = netdev_priv(pdevobj[num]->net);
    else
    {
        DEBUG("ft1000_ChOpen: can not find device object %d\n", num);
        return -1;
    }

    DEBUG("f_owner = %p number of application = %d\n", (&File->f_owner), info->appcnt );

    // Check if maximum number of application exceeded
    if (info->appcnt > MAX_NUM_APP) {
        DEBUG("Maximum number of application exceeded\n");
        return -EACCES;
    }

    // Search for available application info block
    for (i=0; i<MAX_NUM_APP; i++) {
        if ( (info->app_info[i].fileobject == NULL) ) {
            break;
        }
    }

    // Fail due to lack of application info block
    if (i == MAX_NUM_APP) {
        DEBUG("Could not find an application info block\n");
        return -EACCES;
    }

    info->appcnt++;
    info->app_info[i].fileobject = &File->f_owner;
    info->app_info[i].nTxMsg = 0;
    info->app_info[i].nRxMsg = 0;
    info->app_info[i].nTxMsgReject = 0;
    info->app_info[i].nRxMsgMiss = 0;

    File->private_data = pdevobj[num]->net;

	nonseekable_open(Inode, File);
    return 0;
}


//---------------------------------------------------------------------------
// Function:    ft1000_ChPoll
//
// Parameters:
//
// Description:
//
// Notes:
//
//---------------------------------------------------------------------------

static unsigned int ft1000_ChPoll(struct file *file, poll_table *wait)
{
    struct net_device *dev = file->private_data;
	struct ft1000_info *info;
    int i;

    //DEBUG("ft1000_ChPoll called\n");
    if (ft1000_flarion_cnt == 0) {
        DEBUG("FT1000:ft1000_ChPoll called when ft1000_flarion_cnt is zero\n");
        return (-EBADF);
    }

	info = netdev_priv(dev);

    // Search for matching file object
    for (i=0; i<MAX_NUM_APP; i++) {
        if ( info->app_info[i].fileobject == &file->f_owner) {
            //DEBUG("FT1000:ft1000_ChIoctl: Message is for AppId = %d\n", info->app_info[i].app_id);
            break;
        }
    }

    // Could not find application info block
    if (i == MAX_NUM_APP) {
        DEBUG("FT1000:ft1000_ChIoctl:Could not find application info block\n");
        return ( -EACCES );
    }

    if (list_empty(&info->app_info[i].app_sqlist) == 0) {
        DEBUG("FT1000:ft1000_ChPoll:Message detected in slow queue\n");
        return(POLLIN | POLLRDNORM | POLLPRI);
    }

    poll_wait (file, &info->app_info[i].wait_dpram_msg, wait);
    //DEBUG("FT1000:ft1000_ChPoll:Polling for data from DSP\n");

    return (0);
}

//---------------------------------------------------------------------------
// Function:    ft1000_ChIoctl
//
// Parameters:
//
// Description:
//
// Notes:
//
//---------------------------------------------------------------------------
static long ft1000_ChIoctl (struct file *File, unsigned int Command,
                           unsigned long Argument)
{
    void __user *argp = (void __user *)Argument;
    struct net_device *dev;
	struct ft1000_info *info;
    struct ft1000_device *ft1000dev;
    int result=0;
    int cmd;
    int i;
    u16 tempword;
    unsigned long flags;
    struct timeval tv;
    IOCTL_GET_VER get_ver_data;
    IOCTL_GET_DSP_STAT get_stat_data;
    u8 ConnectionMsg[] = {0x00,0x44,0x10,0x20,0x80,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x93,0x64,
                          0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x0a,
                          0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                          0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                          0x00,0x00,0x02,0x37,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x01,0x00,0x01,0x7f,0x00,
                          0x00,0x01,0x00,0x00};

    unsigned short ledStat=0;
    unsigned short conStat=0;

    //DEBUG("ft1000_ChIoctl called\n");

    if (ft1000_flarion_cnt == 0) {
        DEBUG("FT1000:ft1000_ChIoctl called when ft1000_flarion_cnt is zero\n");
        return (-EBADF);
    }

    //DEBUG("FT1000:ft1000_ChIoctl:Command = 0x%x Argument = 0x%8x\n", Command, (u32)Argument);

    dev = File->private_data;
	info = netdev_priv(dev);
    ft1000dev = info->pFt1000Dev;
    cmd = _IOC_NR(Command);
    //DEBUG("FT1000:ft1000_ChIoctl:cmd = 0x%x\n", cmd);

    // process the command
    switch (cmd) {
    case IOCTL_REGISTER_CMD:
            DEBUG("FT1000:ft1000_ChIoctl: IOCTL_FT1000_REGISTER called\n");
            result = get_user(tempword, (__u16 __user*)argp);
            if (result) {
                DEBUG("result = %d failed to get_user\n", result);
                break;
            }
            if (tempword == DSPBCMSGID) {
                // Search for matching file object
                for (i=0; i<MAX_NUM_APP; i++) {
                    if ( info->app_info[i].fileobject == &File->f_owner) {
                        info->app_info[i].DspBCMsgFlag = 1;
                        DEBUG("FT1000:ft1000_ChIoctl:Registered for broadcast messages\n");
                        break;
                    }
                }
            }
            break;

    case IOCTL_GET_VER_CMD:
        DEBUG("FT1000:ft1000_ChIoctl: IOCTL_FT1000_GET_VER called\n");

        get_ver_data.drv_ver = FT1000_DRV_VER;

        if (copy_to_user(argp, &get_ver_data, sizeof(get_ver_data)) ) {
            DEBUG("FT1000:ft1000_ChIoctl: copy fault occurred\n");
            result = -EFAULT;
            break;
        }

        DEBUG("FT1000:ft1000_ChIoctl:driver version = 0x%x\n",(unsigned int)get_ver_data.drv_ver);

        break;
    case IOCTL_CONNECT:
        // Connect Message
        DEBUG("FT1000:ft1000_ChIoctl: IOCTL_FT1000_CONNECT\n");
        ConnectionMsg[79] = 0xfc;
			   CardSendCommand(ft1000dev, (unsigned short *)ConnectionMsg, 0x4c);

        break;
    case IOCTL_DISCONNECT:
        // Disconnect Message
        DEBUG("FT1000:ft1000_ChIoctl: IOCTL_FT1000_DISCONNECT\n");
        ConnectionMsg[79] = 0xfd;
			   CardSendCommand(ft1000dev, (unsigned short *)ConnectionMsg, 0x4c);
        break;
    case IOCTL_GET_DSP_STAT_CMD:
        //DEBUG("FT1000:ft1000_ChIoctl: IOCTL_FT1000_GET_DSP_STAT called\n");
	memset(&get_stat_data, 0, sizeof(get_stat_data));
        memcpy(get_stat_data.DspVer, info->DspVer, DSPVERSZ);
        memcpy(get_stat_data.HwSerNum, info->HwSerNum, HWSERNUMSZ);
        memcpy(get_stat_data.Sku, info->Sku, SKUSZ);
        memcpy(get_stat_data.eui64, info->eui64, EUISZ);

            if (info->ProgConStat != 0xFF) {
                ft1000_read_dpram16(ft1000dev, FT1000_MAG_DSP_LED, (u8 *)&ledStat, FT1000_MAG_DSP_LED_INDX);
                get_stat_data.LedStat = ntohs(ledStat);
                DEBUG("FT1000:ft1000_ChIoctl: LedStat = 0x%x\n", get_stat_data.LedStat);
                ft1000_read_dpram16(ft1000dev, FT1000_MAG_DSP_CON_STATE, (u8 *)&conStat, FT1000_MAG_DSP_CON_STATE_INDX);
                get_stat_data.ConStat = ntohs(conStat);
                DEBUG("FT1000:ft1000_ChIoctl: ConStat = 0x%x\n", get_stat_data.ConStat);
            }
            else {
                get_stat_data.ConStat = 0x0f;
            }


        get_stat_data.nTxPkts = info->stats.tx_packets;
        get_stat_data.nRxPkts = info->stats.rx_packets;
        get_stat_data.nTxBytes = info->stats.tx_bytes;
        get_stat_data.nRxBytes = info->stats.rx_bytes;
        do_gettimeofday ( &tv );
        get_stat_data.ConTm = (u32)(tv.tv_sec - info->ConTm);
        DEBUG("Connection Time = %d\n", (int)get_stat_data.ConTm);
        if (copy_to_user(argp, &get_stat_data, sizeof(get_stat_data)) ) {
            DEBUG("FT1000:ft1000_ChIoctl: copy fault occurred\n");
            result = -EFAULT;
            break;
        }
        DEBUG("ft1000_chioctl: GET_DSP_STAT succeed\n");
        break;
    case IOCTL_SET_DPRAM_CMD:
        {
            IOCTL_DPRAM_BLK *dpram_data = NULL;
            //IOCTL_DPRAM_COMMAND dpram_command;
            u16 qtype;
            u16 msgsz;
		struct pseudo_hdr *ppseudo_hdr;
            u16 *pmsg;
            u16 total_len;
            u16 app_index;
            u16 status;

            //DEBUG("FT1000:ft1000_ChIoctl: IOCTL_FT1000_SET_DPRAM called\n");


            if (ft1000_flarion_cnt == 0) {
                return (-EBADF);
            }

            if (info->DrvMsgPend) {
                return (-ENOTTY);
            }

            if ( (info->DspAsicReset) || (info->fProvComplete == 0) ) {
                return (-EACCES);
            }

            info->fAppMsgPend = 1;

            if (info->CardReady) {

               //DEBUG("FT1000:ft1000_ChIoctl: try to SET_DPRAM \n");

                // Get the length field to see how many bytes to copy
                result = get_user(msgsz, (__u16 __user *)argp);
                msgsz = ntohs (msgsz);
                //DEBUG("FT1000:ft1000_ChIoctl: length of message = %d\n", msgsz);

                if (msgsz > MAX_CMD_SQSIZE) {
                    DEBUG("FT1000:ft1000_ChIoctl: bad message length = %d\n", msgsz);
                    result = -EINVAL;
                    break;
                }

		result = -ENOMEM;
		dpram_data = kmalloc(msgsz + 2, GFP_KERNEL);
		if (!dpram_data)
			break;

                //if ( copy_from_user(&(dpram_command.dpram_blk), (PIOCTL_DPRAM_BLK)Argument, msgsz+2) ) {
                if ( copy_from_user(dpram_data, argp, msgsz+2) ) {
                    DEBUG("FT1000:ft1000_ChIoctl: copy fault occurred\n");
                    result = -EFAULT;
                }
                else {
#if 0
                    // whc - for debugging only
                    ptr = (char *)&dpram_data;
                    for (i=0; i<msgsz; i++) {
                        DEBUG(1,"FT1000:ft1000_ChIoctl: data %d = 0x%x\n", i, *ptr++);
                    }
#endif
                    // Check if this message came from a registered application
                    for (i=0; i<MAX_NUM_APP; i++) {
                        if ( info->app_info[i].fileobject == &File->f_owner) {
                            break;
                        }
                    }
                    if (i==MAX_NUM_APP) {
                        DEBUG("FT1000:No matching application fileobject\n");
                        result = -EINVAL;
			kfree(dpram_data);
                        break;
                    }
                    app_index = i;

                    // Check message qtype type which is the lower byte within qos_class
                    //qtype = ntohs(dpram_command.dpram_blk.pseudohdr.qos_class) & 0xff;
                    qtype = ntohs(dpram_data->pseudohdr.qos_class) & 0xff;
                    //DEBUG("FT1000_ft1000_ChIoctl: qtype = %d\n", qtype);
                    if (qtype) {
                    }
                    else {
                        // Put message into Slow Queue
                        // Only put a message into the DPRAM if msg doorbell is available
                        status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_DOORBELL);
                        //DEBUG("FT1000_ft1000_ChIoctl: READ REGISTER tempword=%x\n", tempword);
                        if (tempword & FT1000_DB_DPRAM_TX) {
                            // Suspend for 2ms and try again due to DSP doorbell busy
                            mdelay(2);
                            status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_DOORBELL);
                            if (tempword & FT1000_DB_DPRAM_TX) {
                                // Suspend for 1ms and try again due to DSP doorbell busy
                                mdelay(1);
                                status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_DOORBELL);
                                if (tempword & FT1000_DB_DPRAM_TX) {
                                    status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_DOORBELL);
                                    if (tempword & FT1000_DB_DPRAM_TX) {
                                        // Suspend for 3ms and try again due to DSP doorbell busy
                                        mdelay(3);
                                        status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_DOORBELL);
                                        if (tempword & FT1000_DB_DPRAM_TX) {
                                            DEBUG("FT1000:ft1000_ChIoctl:Doorbell not available\n");
                                            result = -ENOTTY;
						kfree(dpram_data);
                                            break;
                                        }
                                    }
                                }
                            }
                        }

                        //DEBUG("FT1000_ft1000_ChIoctl: finished reading register\n");

                        // Make sure we are within the limits of the slow queue memory limitation
                        if ( (msgsz < MAX_CMD_SQSIZE) && (msgsz > PSEUDOSZ) ) {
                            // Need to put sequence number plus new checksum for message
                            //pmsg = (u16 *)&dpram_command.dpram_blk.pseudohdr;
                            pmsg = (u16 *)&dpram_data->pseudohdr;
				ppseudo_hdr = (struct pseudo_hdr *)pmsg;
                            total_len = msgsz+2;
                            if (total_len & 0x1) {
                                total_len++;
                            }

                            // Insert slow queue sequence number
                            ppseudo_hdr->seq_num = info->squeseqnum++;
                            ppseudo_hdr->portsrc = info->app_info[app_index].app_id;
                            // Calculate new checksum
                            ppseudo_hdr->checksum = *pmsg++;
                            //DEBUG("checksum = 0x%x\n", ppseudo_hdr->checksum);
                            for (i=1; i<7; i++) {
                                ppseudo_hdr->checksum ^= *pmsg++;
                                //DEBUG("checksum = 0x%x\n", ppseudo_hdr->checksum);
                            }
                            pmsg++;
				ppseudo_hdr = (struct pseudo_hdr *)pmsg;
#if 0
                            ptr = dpram_data;
                            DEBUG("FT1000:ft1000_ChIoctl: Command Send\n");
                            for (i=0; i<total_len; i++) {
                                DEBUG("FT1000:ft1000_ChIoctl: data %d = 0x%x\n", i, *ptr++);
                            }
#endif
                            //dpram_command.extra = 0;

                            //CardSendCommand(ft1000dev,(unsigned char*)&dpram_command,total_len+2);
                            CardSendCommand(ft1000dev,(unsigned short*)dpram_data,total_len+2);


                            info->app_info[app_index].nTxMsg++;
                        }
                        else {
                            result = -EINVAL;
                        }
                    }
                }
            }
            else {
                DEBUG("FT1000:ft1000_ChIoctl: Card not ready take messages\n");
                result = -EACCES;
            }
	    kfree(dpram_data);

        }
        break;
    case IOCTL_GET_DPRAM_CMD:
        {
		struct dpram_blk *pdpram_blk;
            IOCTL_DPRAM_BLK __user *pioctl_dpram;
            int msglen;

            //DEBUG("FT1000:ft1000_ChIoctl: IOCTL_FT1000_GET_DPRAM called\n");

            if (ft1000_flarion_cnt == 0) {
                return (-EBADF);
            }

            // Search for matching file object
            for (i=0; i<MAX_NUM_APP; i++) {
                if ( info->app_info[i].fileobject == &File->f_owner) {
                    //DEBUG("FT1000:ft1000_ChIoctl: Message is for AppId = %d\n", info->app_info[i].app_id);
                    break;
                }
            }

            // Could not find application info block
            if (i == MAX_NUM_APP) {
                DEBUG("FT1000:ft1000_ChIoctl:Could not find application info block\n");
                result = -EBADF;
                break;
            }

            result = 0;
            pioctl_dpram = argp;
            if (list_empty(&info->app_info[i].app_sqlist) == 0) {
                //DEBUG("FT1000:ft1000_ChIoctl:Message detected in slow queue\n");
                spin_lock_irqsave(&free_buff_lock, flags);
                pdpram_blk = list_entry(info->app_info[i].app_sqlist.next, struct dpram_blk, list);
                list_del(&pdpram_blk->list);
                info->app_info[i].NumOfMsg--;
                //DEBUG("FT1000:ft1000_ChIoctl:NumOfMsg for app %d = %d\n", i, info->app_info[i].NumOfMsg);
                spin_unlock_irqrestore(&free_buff_lock, flags);
                msglen = ntohs(*(u16 *)pdpram_blk->pbuffer) + PSEUDOSZ;
                result = get_user(msglen, &pioctl_dpram->total_len);
		if (result)
			break;
		msglen = htons(msglen);
                //DEBUG("FT1000:ft1000_ChIoctl:msg length = %x\n", msglen);
                if(copy_to_user (&pioctl_dpram->pseudohdr, pdpram_blk->pbuffer, msglen))
				{
					DEBUG("FT1000:ft1000_ChIoctl: copy fault occurred\n");
	             	result = -EFAULT;
	             	break;
				}

                ft1000_free_buffer(pdpram_blk, &freercvpool);
                result = msglen;
            }
            //DEBUG("FT1000:ft1000_ChIoctl: IOCTL_FT1000_GET_DPRAM no message\n");
        }
        break;

    default:
        DEBUG("FT1000:ft1000_ChIoctl:unknown command: 0x%x\n", Command);
        result = -ENOTTY;
        break;
    }
    info->fAppMsgPend = 0;
    return result;
}

//---------------------------------------------------------------------------
// Function:    ft1000_ChRelease
//
// Parameters:
//
// Description:
//
// Notes:
//
//---------------------------------------------------------------------------
static int ft1000_ChRelease (struct inode *Inode, struct file *File)
{
	struct ft1000_info *info;
    struct net_device *dev;
    int i;
	struct dpram_blk *pdpram_blk;

    DEBUG("ft1000_ChRelease called\n");

    dev = File->private_data;
	info = netdev_priv(dev);

    if (ft1000_flarion_cnt == 0) {
        info->appcnt--;
        return (-EBADF);
    }

    // Search for matching file object
    for (i=0; i<MAX_NUM_APP; i++) {
        if ( info->app_info[i].fileobject == &File->f_owner) {
            //DEBUG("FT1000:ft1000_ChIoctl: Message is for AppId = %d\n", info->app_info[i].app_id);
            break;
        }
    }

    if (i==MAX_NUM_APP)
	    return 0;

    while (list_empty(&info->app_info[i].app_sqlist) == 0) {
        DEBUG("Remove and free memory queue up on slow queue\n");
        pdpram_blk = list_entry(info->app_info[i].app_sqlist.next, struct dpram_blk, list);
        list_del(&pdpram_blk->list);
        ft1000_free_buffer(pdpram_blk, &freercvpool);
    }

    // initialize application information
    info->appcnt--;
    DEBUG("ft1000_chdev:%s:appcnt = %d\n", __FUNCTION__, info->appcnt);
    info->app_info[i].fileobject = NULL;

    return 0;
}

