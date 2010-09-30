//=====================================================
// CopyRight (C) 2007 Qualcomm Inc. All Rights Reserved.
//
//
// This file is part of Express Card USB Driver
//
// $Id:
//====================================================
// 20090926; aelias; removed compiler warnings & errors; ubuntu 9.04; 2.6.28-15-generic

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/usb.h>
#include "ft1000_usb.h"
#include <linux/types.h>
//#include <asm/semaphore.h>		//aelias [-] reason : file moved
//#include <linux/semaphore.h>		//aelias [+] reason : file moved
//#include <asm/io.h>
//#include <linux/kthread.h>

#define HARLEY_READ_REGISTER     0x0
#define HARLEY_WRITE_REGISTER    0x01
#define HARLEY_READ_DPRAM_32     0x02
#define HARLEY_READ_DPRAM_LOW    0x03
#define HARLEY_READ_DPRAM_HIGH   0x04
#define HARLEY_WRITE_DPRAM_32    0x05
#define HARLEY_WRITE_DPRAM_LOW   0x06
#define HARLEY_WRITE_DPRAM_HIGH  0x07

#define HARLEY_READ_OPERATION    0xc1
#define HARLEY_WRITE_OPERATION   0x41

//#define JDEBUG

static int ft1000_reset(struct net_device *ft1000dev);
static int ft1000_submit_rx_urb(PFT1000_INFO info);
static void ft1000_hbchk(u_long data);
static int ft1000_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int ft1000_open (struct net_device *dev);
static struct net_device_stats *ft1000_netdev_stats(struct net_device *dev);
static struct timer_list poll_timer[MAX_NUM_CARDS];
static int ft1000_chkcard (struct ft1000_device *dev);
/*
static const struct net_device_ops ft1000net_ops = {
    .ndo_start_xmit = ft1000_start_xmit,
    .ndo_get_stats = ft1000_netdev_stats,
    .ndo_open = ft1000_open,
    .ndo_stop = ft1000_close,
};
*/

//Jim

static u8 tempbuffer[1600];
static int gCardIndex;

#define MAX_RCV_LOOP   100


static int atoi(const char *s)
{
        int k = 0;
    int cnt;

        k = 0;
    cnt = 0;
        while (*s != '\0' && *s >= '0' && *s <= '9') {
                k = 10 * k + (*s - '0');
                s++;
        // Let's put a limit on this while loop to avoid deadlock scenario
        if (cnt > 100)
           break;
        cnt++;
        }
        return k;
}
/****************************************************************
 *     ft1000_control_complete
 ****************************************************************/
static void ft1000_control_complete(struct urb *urb)
{
    struct ft1000_device *ft1000dev = (struct ft1000_device *)urb->context;

    //DEBUG("FT1000_CONTROL_COMPLETE ENTERED\n");
    if (ft1000dev == NULL )
    {
        DEBUG("NULL ft1000dev, failure\n");
        return ;
    }
    else if ( ft1000dev->dev == NULL )
    {
        DEBUG("NULL ft1000dev->dev, failure\n");
        return ;
    }
    //spin_lock(&ft1000dev->device_lock);

    if(waitqueue_active(&ft1000dev->control_wait))
    {
        wake_up(&ft1000dev->control_wait);
    }

    //DEBUG("FT1000_CONTROL_COMPLETE RETURNED\n");
    //spin_unlock(&ft1000dev->device_lock);
}

//---------------------------------------------------------------------------
// Function:    ft1000_control
//
// Parameters:  ft1000_device  - device structure
//              pipe - usb control message pipe
//              request - control request
//              requesttype - control message request type
//              value - value to be written or 0
//              index - register index
//              data - data buffer to hold the read/write values
//              size - data size
//              timeout - control message time out value
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function sends a control message via USB interface synchronously
//
// Notes:
//
//---------------------------------------------------------------------------
static int ft1000_control(struct ft1000_device *ft1000dev,unsigned int pipe,
                          u8 request,
                          u8 requesttype,
                          u16 value,
                          u16 index,
                          void *data,
                          u16 size,
                          int timeout)
{
	u16 ret;

    if (ft1000dev == NULL )
    {
        DEBUG("NULL ft1000dev, failure\n");
        return STATUS_FAILURE;
    }
    else if ( ft1000dev->dev == NULL )
    {
        DEBUG("NULL ft1000dev->dev, failure\n");
        return STATUS_FAILURE;
    }

    ret = usb_control_msg(ft1000dev->dev,
                          pipe,
                          request,
                          requesttype,
                          value,
                          index,
                          data,
                          size,
                          LARGE_TIMEOUT);

    if (ret>0)
        ret = STATUS_SUCCESS;
    else
        ret = STATUS_FAILURE;


    return ret;


}
//---------------------------------------------------------------------------
// Function:    ft1000_read_register
//
// Parameters:  ft1000_device  - device structure
//              Data - data buffer to hold the value read
//              nRegIndex - register index
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function returns the value in a register
//
// Notes:
//
//---------------------------------------------------------------------------

u16 ft1000_read_register(struct ft1000_device *ft1000dev, u16* Data, u16 nRegIndx)
{
    u16 ret = STATUS_SUCCESS;

    //DEBUG("ft1000_read_register: reg index is %d\n", nRegIndx);
    //DEBUG("ft1000_read_register: spin_lock locked\n");
    ret = ft1000_control(ft1000dev,
                         usb_rcvctrlpipe(ft1000dev->dev,0),
                         HARLEY_READ_REGISTER,   //request --READ_REGISTER
                         HARLEY_READ_OPERATION,  //requestType
                         0,                      //value
                         nRegIndx,               //index
                         Data,                   //data
                         2,                      //data size
                         LARGE_TIMEOUT );        //timeout

   //DEBUG("ft1000_read_register: ret is  %d \n", ret);

   //DEBUG("ft1000_read_register: data is  %x \n", *Data);
   if ( ret != STATUS_SUCCESS )
       return STATUS_FAILURE;

   return ret;

}

//---------------------------------------------------------------------------
// Function:    ft1000_write_register
//
// Parameters:  ft1000_device  - device structure
//              value - value to write into a register
//              nRegIndex - register index
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function writes the value in a register
//
// Notes:
//
//---------------------------------------------------------------------------
u16 ft1000_write_register(struct ft1000_device *ft1000dev, USHORT value, u16 nRegIndx)
{
     u16 ret = STATUS_SUCCESS;

     //DEBUG("ft1000_write_register: value is: %d, reg index is: %d\n", value, nRegIndx);

     ret = ft1000_control(ft1000dev,
                           usb_sndctrlpipe(ft1000dev->dev, 0),
                           HARLEY_WRITE_REGISTER,       //request -- WRITE_REGISTER
                           HARLEY_WRITE_OPERATION,      //requestType
                           value,
                           nRegIndx,
                           NULL,
                           0,
                           LARGE_TIMEOUT );

   if ( ret != STATUS_SUCCESS )
       return STATUS_FAILURE;

    return ret;
}

//---------------------------------------------------------------------------
// Function:    ft1000_read_dpram32
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to read
//              buffer - data buffer to hold the data read
//              cnt - number of byte read from DPRAM
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function read a number of bytes from DPRAM
//
// Notes:
//
//---------------------------------------------------------------------------

u16 ft1000_read_dpram32(struct ft1000_device *ft1000dev, USHORT indx, PUCHAR buffer, USHORT cnt)
{
    u16 ret = STATUS_SUCCESS;

    //DEBUG("ft1000_read_dpram32: indx: %d  cnt: %d\n", indx, cnt);
    ret =ft1000_control(ft1000dev,
                         usb_rcvctrlpipe(ft1000dev->dev,0),
                         HARLEY_READ_DPRAM_32,                //request --READ_DPRAM_32
                         HARLEY_READ_OPERATION,               //requestType
                         0,                                   //value
                         indx,                                //index
                         buffer,                              //data
                         cnt,                                 //data size
                         LARGE_TIMEOUT );                     //timeout

   //DEBUG("ft1000_read_dpram32: ret is  %d \n", ret);

   //DEBUG("ft1000_read_dpram32: ret=%d \n", ret);
   if ( ret != STATUS_SUCCESS )
       return STATUS_FAILURE;

   return ret;

}

//---------------------------------------------------------------------------
// Function:    ft1000_write_dpram32
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to write the data
//              buffer - data buffer to write into DPRAM
//              cnt - number of bytes to write
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function writes into DPRAM a number of bytes
//
// Notes:
//
//---------------------------------------------------------------------------
u16 ft1000_write_dpram32(struct ft1000_device *ft1000dev, USHORT indx, PUCHAR buffer, USHORT cnt)
{
     u16 ret = STATUS_SUCCESS;

     //DEBUG("ft1000_write_dpram32: indx: %d   buffer: %x cnt: %d\n", indx, buffer, cnt);
     if ( cnt % 4)
         cnt += cnt - (cnt % 4);

     ret = ft1000_control(ft1000dev,
                           usb_sndctrlpipe(ft1000dev->dev, 0),
                           HARLEY_WRITE_DPRAM_32,              //request -- WRITE_DPRAM_32
                           HARLEY_WRITE_OPERATION,             //requestType
                           0,                                  //value
                           indx,                               //index
                           buffer,                             //buffer
                           cnt,                                //buffer size
                           LARGE_TIMEOUT );


   if ( ret != STATUS_SUCCESS )
       return STATUS_FAILURE;


    return ret;
}

//---------------------------------------------------------------------------
// Function:    ft1000_read_dpram16
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to read
//              buffer - data buffer to hold the data read
//              hightlow - high or low 16 bit word
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function read 16 bits from DPRAM
//
// Notes:
//
//---------------------------------------------------------------------------
u16 ft1000_read_dpram16(struct ft1000_device *ft1000dev, USHORT indx, PUCHAR buffer, u8 highlow)
{
    u16 ret = STATUS_SUCCESS;

    //DEBUG("ft1000_read_dpram16: indx: %d  hightlow: %d\n", indx, highlow);

    u8 request;

    if (highlow == 0 )
        request = HARLEY_READ_DPRAM_LOW;
    else
        request = HARLEY_READ_DPRAM_HIGH;

    ret = ft1000_control(ft1000dev,
                         usb_rcvctrlpipe(ft1000dev->dev,0),
                         request,                     //request --READ_DPRAM_H/L
                         HARLEY_READ_OPERATION,       //requestType
                         0,                           //value
                         indx,                        //index
                         buffer,                      //data
                         2,                           //data size
                         LARGE_TIMEOUT );             //timeout

   //DEBUG("ft1000_read_dpram16: ret is  %d \n", ret);


   //DEBUG("ft1000_read_dpram16: data is  %x \n", *buffer);
   if ( ret != STATUS_SUCCESS )
       return STATUS_FAILURE;

   return ret;

}

//---------------------------------------------------------------------------
// Function:    ft1000_write_dpram16
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to write the data
//              value - 16bits value to write
//              hightlow - high or low 16 bit word
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function writes into DPRAM a number of bytes
//
// Notes:
//
//---------------------------------------------------------------------------
u16 ft1000_write_dpram16(struct ft1000_device *ft1000dev, USHORT indx, USHORT value, u8 highlow)
{
     u16 ret = STATUS_SUCCESS;



     //DEBUG("ft1000_write_dpram16: indx: %d  value: %d  highlow: %d\n", indx, value, highlow);

     u8 request;


     if ( highlow == 0 )
         request = HARLEY_WRITE_DPRAM_LOW;
     else
         request = HARLEY_WRITE_DPRAM_HIGH;

     ret = ft1000_control(ft1000dev,
                           usb_sndctrlpipe(ft1000dev->dev, 0),
                           request,                             //request -- WRITE_DPRAM_H/L
                           HARLEY_WRITE_OPERATION,              //requestType
                           value,                                   //value
                           indx,                                //index
                           NULL,                               //buffer
                           0,                                   //buffer size
                           LARGE_TIMEOUT );


   if ( ret != STATUS_SUCCESS )
       return STATUS_FAILURE;


    return ret;
}

//---------------------------------------------------------------------------
// Function:    fix_ft1000_read_dpram32
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to read
//              buffer - data buffer to hold the data read
//
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function read DPRAM 4 words at a time
//
// Notes:
//
//---------------------------------------------------------------------------
u16 fix_ft1000_read_dpram32(struct ft1000_device *ft1000dev, USHORT indx, PUCHAR buffer)
{
    UCHAR buf[16];
    USHORT pos;
    u16 ret = STATUS_SUCCESS;

    //DEBUG("fix_ft1000_read_dpram32: indx: %d  \n", indx);
    pos = (indx / 4)*4;
    ret = ft1000_read_dpram32(ft1000dev, pos, buf, 16);
    if (ret == STATUS_SUCCESS)
    {
        pos = (indx % 4)*4;
        *buffer++ = buf[pos++];
        *buffer++ = buf[pos++];
        *buffer++ = buf[pos++];
        *buffer++ = buf[pos++];
    }
    else
    {
        DEBUG("fix_ft1000_read_dpram32: DPRAM32 Read failed\n");
        *buffer++ = 0;
        *buffer++ = 0;
        *buffer++ = 0;
        *buffer++ = 0;

    }

   //DEBUG("fix_ft1000_read_dpram32: data is  %x \n", *buffer);
   return ret;

}


//---------------------------------------------------------------------------
// Function:    fix_ft1000_write_dpram32
//
// Parameters:  ft1000_device  - device structure
//              indx - starting address to write
//              buffer - data buffer to write
//
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function write to DPRAM 4 words at a time
//
// Notes:
//
//---------------------------------------------------------------------------
u16 fix_ft1000_write_dpram32(struct ft1000_device *ft1000dev, USHORT indx, PUCHAR buffer)
{
    USHORT pos1;
    USHORT pos2;
    USHORT i;
    UCHAR buf[32];
    UCHAR resultbuffer[32];
    PUCHAR pdata;
    u16 ret  = STATUS_SUCCESS;

    //DEBUG("fix_ft1000_write_dpram32: Entered:\n");

    pos1 = (indx / 4)*4;
    pdata = buffer;
    ret = ft1000_read_dpram32(ft1000dev, pos1, buf, 16);
    if (ret == STATUS_SUCCESS)
    {
        pos2 = (indx % 4)*4;
        buf[pos2++] = *buffer++;
        buf[pos2++] = *buffer++;
        buf[pos2++] = *buffer++;
        buf[pos2++] = *buffer++;
        ret = ft1000_write_dpram32(ft1000dev, pos1, buf, 16);
    }
    else
    {
        DEBUG("fix_ft1000_write_dpram32: DPRAM32 Read failed\n");

        return ret;
    }

    ret = ft1000_read_dpram32(ft1000dev, pos1, (PUCHAR)&resultbuffer[0], 16);
    if (ret == STATUS_SUCCESS)
    {
        buffer = pdata;
        for (i=0; i<16; i++)
        {
            if (buf[i] != resultbuffer[i]){

                ret = STATUS_FAILURE;
            }
        }
    }

    if (ret == STATUS_FAILURE)
    {
        ret = ft1000_write_dpram32(ft1000dev, pos1, (PUCHAR)&tempbuffer[0], 16);
        ret = ft1000_read_dpram32(ft1000dev, pos1, (PUCHAR)&resultbuffer[0], 16);
        if (ret == STATUS_SUCCESS)
        {
            buffer = pdata;
            for (i=0; i<16; i++)
            {
                if (tempbuffer[i] != resultbuffer[i])
                {
                    ret = STATUS_FAILURE;
                    DEBUG("fix_ft1000_write_dpram32 Failed to write\n");
                }
            }
         }
    }

    return ret;

}


//------------------------------------------------------------------------
//
//  Function:   card_reset_dsp
//
//  Synopsis:   This function is called to reset or activate the DSP
//
//  Arguments:  value                  - reset or activate
//
//  Returns:    None
//-----------------------------------------------------------------------
static void card_reset_dsp (struct ft1000_device *ft1000dev, BOOLEAN value)
{
    u16 status = STATUS_SUCCESS;
    USHORT tempword;

    status = ft1000_write_register (ft1000dev, HOST_INTF_BE, FT1000_REG_SUP_CTRL);
    status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_SUP_CTRL);
    if (value)
    {
        DEBUG("Reset DSP\n");
        status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_RESET);
        tempword |= DSP_RESET_BIT;
        status = ft1000_write_register(ft1000dev, tempword, FT1000_REG_RESET);
    }
    else
    {
        DEBUG("Activate DSP\n");
        status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_RESET);
#if 1
        tempword |= DSP_ENCRYPTED;
        tempword &= ~DSP_UNENCRYPTED;
#else
        tempword |= DSP_UNENCRYPTED;
        tempword &= ~DSP_ENCRYPTED;
#endif
        status = ft1000_write_register(ft1000dev, tempword, FT1000_REG_RESET);
        status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_RESET);
        tempword &= ~EFUSE_MEM_DISABLE;
        tempword &= ~DSP_RESET_BIT;
        status = ft1000_write_register(ft1000dev, tempword, FT1000_REG_RESET);
        status = ft1000_read_register(ft1000dev, &tempword, FT1000_REG_RESET);
    }
}

//---------------------------------------------------------------------------
// Function:    CardSendCommand
//
// Parameters:  ft1000_device  - device structure
//              ptempbuffer - command buffer
//              size - command buffer size
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function sends a command to ASIC
//
// Notes:
//
//---------------------------------------------------------------------------
void CardSendCommand(struct ft1000_device *ft1000dev, void *ptempbuffer, int size)
{
    unsigned short temp;
    unsigned char *commandbuf;

    DEBUG("CardSendCommand: enter CardSendCommand... size=%d\n", size);

    commandbuf =(unsigned char*) kmalloc(size+2, GFP_KERNEL);
    //memset((void*)commandbuf, 0, size+2);
    memcpy((void*)commandbuf+2, (void*)ptempbuffer, size);

    //DEBUG("CardSendCommand: Command Send\n");
    /***
    for (i=0; i<size+2; i++)
    {
        DEBUG("FT1000:ft1000_ChIoctl: data %d = 0x%x\n", i, *ptr++);
    }
    ***/

    ft1000_read_register(ft1000dev, &temp, FT1000_REG_DOORBELL);

    if (temp & 0x0100)
    {
       msleep(10);
    }

    // check for odd word
    size = size + 2;
    if (size % 4)
    {
       // Must force to be 32 bit aligned
       size += 4 - (size % 4);
    }


    //DEBUG("CardSendCommand: write dpram ... size=%d\n", size);
    ft1000_write_dpram32(ft1000dev, 0,commandbuf, size);
    msleep(1);
    //DEBUG("CardSendCommand: write into doorbell ...\n");
    ft1000_write_register(ft1000dev,  FT1000_DB_DPRAM_TX ,FT1000_REG_DOORBELL) ;
    msleep(1);

    ft1000_read_register(ft1000dev, &temp, FT1000_REG_DOORBELL);
    //DEBUG("CardSendCommand: read doorbell ...temp=%x\n", temp);
    if ( (temp & 0x0100) == 0)
    {
       //DEBUG("CardSendCommand: Message sent\n");
    }

}


//--------------------------------------------------------------------------
//
//  Function:   dsp_reload
//
//  Synopsis:   This function is called to load or reload the DSP
//
//  Arguments:  ft1000dev - device structure
//
//  Returns:    None
//-----------------------------------------------------------------------
void dsp_reload (struct ft1000_device *ft1000dev)
{
    u16 status;
    USHORT tempword;
    ULONG templong;

    PFT1000_INFO pft1000info;

    pft1000info = netdev_priv(ft1000dev->net);

    pft1000info->CardReady = 0;
    pft1000info->DSP_loading= 1;

    // Program Interrupt Mask register
    status = ft1000_write_register (ft1000dev, 0xffff, FT1000_REG_SUP_IMASK);

    status = ft1000_read_register (ft1000dev, &tempword, FT1000_REG_RESET);
    tempword |= ASIC_RESET_BIT;
    status = ft1000_write_register (ft1000dev, tempword, FT1000_REG_RESET);
    msleep(1000);
    status = ft1000_read_register (ft1000dev, &tempword, FT1000_REG_RESET);
    DEBUG("Reset Register = 0x%x\n", tempword);

    // Toggle DSP reset
    card_reset_dsp (ft1000dev, 1);
    msleep(1000);
    card_reset_dsp (ft1000dev, 0);
    msleep(1000);

    status = ft1000_write_register (ft1000dev, HOST_INTF_BE, FT1000_REG_SUP_CTRL);

    // Let's check for FEFE
    status = ft1000_read_dpram32 (ft1000dev, FT1000_MAG_DPRAM_FEFE_INDX, (PUCHAR)&templong, 4);
    DEBUG("templong (fefe) = 0x%8x\n", templong);

    // call codeloader
    status = scram_dnldr(ft1000dev, pFileStart, FileLength);

    if ( status != STATUS_SUCCESS)
       return;

    msleep(1000);
    pft1000info->DSP_loading= 0;

    DEBUG("dsp_reload returned\n");


}

//---------------------------------------------------------------------------
//
// Function:   ft1000_reset_asic
// Descripton: This function will call the Card Service function to reset the
//             ASIC.
// Input:
//     dev    - device structure
// Output:
//     none
//
//---------------------------------------------------------------------------
static void ft1000_reset_asic (struct net_device *dev)
{
    FT1000_INFO *info = netdev_priv(dev);
    struct ft1000_device *ft1000dev = info->pFt1000Dev;
    u16 tempword;

    DEBUG("ft1000_hw:ft1000_reset_asic called\n");

    info->ASICResetNum++;

    // Let's use the register provided by the Magnemite ASIC to reset the
    // ASIC and DSP.
    ft1000_write_register(ft1000dev,  (DSP_RESET_BIT | ASIC_RESET_BIT), FT1000_REG_RESET );

    mdelay(1);

    // set watermark to -1 in order to not generate an interrrupt
    ft1000_write_register(ft1000dev, 0xffff, FT1000_REG_MAG_WATERMARK);

    // clear interrupts
    ft1000_read_register (ft1000dev, &tempword, FT1000_REG_SUP_ISR);
    DEBUG("ft1000_hw: interrupt status register = 0x%x\n",tempword);
    ft1000_write_register (ft1000dev,  tempword, FT1000_REG_SUP_ISR);
    ft1000_read_register (ft1000dev, &tempword, FT1000_REG_SUP_ISR);
    DEBUG("ft1000_hw: interrupt status register = 0x%x\n",tempword);

}
/*
//---------------------------------------------------------------------------
//
// Function:   ft1000_disable_interrupts
// Descripton: This function will disable all interrupts.
// Input:
//     dev    - device structure
// Output:
//     None.
//
//---------------------------------------------------------------------------
static void ft1000_disable_interrupts(struct net_device *dev) {
    FT1000_INFO *info = netdev_priv(dev);
    struct ft1000_device *ft1000dev = info->pFt1000Dev;
    u16 tempword;

    DEBUG("ft1000_hw: ft1000_disable_interrupts()\n");
    ft1000_write_register (ft1000dev, ISR_MASK_ALL, FT1000_REG_SUP_IMASK);
    ft1000_read_register (ft1000dev, &tempword, FT1000_REG_SUP_IMASK);
    DEBUG("ft1000_hw:ft1000_disable_interrupts:current interrupt enable mask = 0x%x\n", tempword);
    info->InterruptsEnabled = FALSE;
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_enable_interrupts
// Descripton: This function will enable interrupts base on the current interrupt mask.
// Input:
//     dev    - device structure
// Output:
//     None.
//
//---------------------------------------------------------------------------
static void ft1000_enable_interrupts(struct net_device *dev) {
    FT1000_INFO *info = netdev_priv(dev);
    struct ft1000_device *ft1000dev = info->pFt1000Dev;
    u16 tempword;

    DEBUG("ft1000_hw:ft1000_enable_interrupts()\n");
    ft1000_write_register (ft1000dev, info->CurrentInterruptEnableMask, FT1000_REG_SUP_IMASK);
    ft1000_read_register (ft1000dev, &tempword, FT1000_REG_SUP_IMASK);
    DEBUG("ft1000_hw:ft1000_enable_interrupts:current interrupt enable mask = 0x%x\n", tempword);
    info->InterruptsEnabled = TRUE;
}
*/

//---------------------------------------------------------------------------
//
// Function:   ft1000_reset_card
// Descripton: This function will reset the card
// Input:
//     dev    - device structure
// Output:
//     status - FALSE (card reset fail)
//              TRUE  (card reset successful)
//
//---------------------------------------------------------------------------
static int ft1000_reset_card (struct net_device *dev)
{
    FT1000_INFO *info = netdev_priv(dev);
    struct ft1000_device *ft1000dev = info->pFt1000Dev;
    u16 tempword;
    PPROV_RECORD ptr;

    DEBUG("ft1000_hw:ft1000_reset_card called.....\n");

    info->fCondResetPend = 1;
    info->CardReady = 0;
    info->fProvComplete = 0;
    //ft1000_disable_interrupts(dev);

    // Cancel heartbeat task since we are reloading the dsp
    //del_timer(&poll_timer[info->CardNumber]);

    // Make sure we free any memory reserve for provisioning
    while (list_empty(&info->prov_list) == 0) {
        DEBUG("ft1000_hw:ft1000_reset_card:deleting provisioning record\n");
        ptr = list_entry(info->prov_list.next, PROV_RECORD, list);
        list_del(&ptr->list);
        kfree(ptr->pprov_data);
        kfree(ptr);
    }

    DEBUG("ft1000_hw:ft1000_reset_card: reset asic\n");
    //reset ASIC
    ft1000_reset_asic(dev);

    info->DSPResetNum++;

#if 0
    DEBUG("ft1000_hw:ft1000_reset_card:resetting ASIC and DSP\n");
    ft1000_write_register (ft1000dev, (DSP_RESET_BIT | ASIC_RESET_BIT), FT1000_REG_RESET );


    // Copy DSP session record into info block if this is not a coldstart
    //if (ft1000_card_present == 1) {
        spin_lock_irqsave(&info->dpram_lock, flags);

            ft1000_write_register(ft1000dev,  FT1000_DPRAM_MAG_RX_BASE, FT1000_REG_DPRAM_ADDR);
            for (i=0;i<MAX_DSP_SESS_REC/2; i++) {
                //info->DSPSess.MagRec[i] = inl(dev->base_addr+FT1000_REG_MAG_DPDATA);
                ft1000_read_dpram32(ft1000dev, FT1000_REG_MAG_DPDATA, (PCHAR)&(info->DSPSess.MagRec[i]), 4);
            }

        spin_unlock_irqrestore(&info->dpram_lock, flags);
    //}
    info->squeseqnum = 0;

    DEBUG("ft1000_hw:ft1000_reset_card:resetting ASIC\n");
    mdelay(10);
    //reset ASIC
    ft1000_reset_asic(dev);

    info->DSPResetNum++;

    DEBUG("ft1000_hw:ft1000_reset_card:downloading dsp image\n");


        // Put dsp in reset and take ASIC out of reset
        DEBUG("ft1000_hw:ft1000_reset_card:Put DSP in reset and take ASIC out of reset\n");
        ft1000_write_register (ft1000dev, DSP_RESET_BIT, FT1000_REG_RESET);

        // Setting MAGNEMITE ASIC to big endian mode
        ft1000_write_register (ft1000dev, HOST_INTF_BE, FT1000_REG_SUP_CTRL);

        // Take DSP out of reset

           ft1000_read_register(ft1000dev, &tempword, FT1000_REG_RESET);
           tempword |= DSP_ENCRYPTED;
           tempword &= ~DSP_UNENCRYPTED;
           ft1000_write_register(ft1000dev, tempword, FT1000_REG_RESET);
           tempword &= ~EFUSE_MEM_DISABLE;
           ft1000_write_register(ft1000dev, tempword, FT1000_REG_RESET);
           tempword &= ~DSP_RESET_BIT;
           ft1000_write_register(ft1000dev, tempword, FT1000_REG_RESET);


        // FLARION_DSP_ACTIVE;
        mdelay(10);
        DEBUG("ft1000_hw:ft1000_reset_card:Take DSP out of reset\n");

        // Wait for 0xfefe indicating dsp ready before starting download
        for (i=0; i<50; i++) {
            //tempword = ft1000_read_dpram_mag_16(dev, FT1000_MAG_DPRAM_FEFE, FT1000_MAG_DPRAM_FEFE_INDX);
            ft1000_read_dpram32 (ft1000dev, FT1000_MAG_DPRAM_FEFE_INDX, (PUCHAR)&templong, 4);
            if (tempword == 0xfefe) {
                break;
            }
            mdelay(20);
        }

        if (i==50) {
            DEBUG("ft1000_hw:ft1000_reset_card:No FEFE detected from DSP\n");
            return FALSE;
        }


#endif

    DEBUG("ft1000_hw:ft1000_reset_card: call dsp_reload\n");
    dsp_reload(ft1000dev);

    DEBUG("dsp reload successful\n");


    mdelay(10);

    // Initialize DSP heartbeat area to ho
    ft1000_write_dpram16(ft1000dev, FT1000_MAG_HI_HO, ho_mag, FT1000_MAG_HI_HO_INDX);
    ft1000_read_dpram16(ft1000dev, FT1000_MAG_HI_HO, (PCHAR)&tempword, FT1000_MAG_HI_HO_INDX);
    DEBUG("ft1000_hw:ft1000_reset_card:hi_ho value = 0x%x\n", tempword);



    info->CardReady = 1;
    //ft1000_enable_interrupts(dev);
    /* Schedule heartbeat process to run every 2 seconds */
    //poll_timer[info->CardNumber].expires = jiffies + (2*HZ);
    //poll_timer[info->CardNumber].data = (u_long)dev;
    //add_timer(&poll_timer[info->CardNumber]);

    info->fCondResetPend = 0;
    return TRUE;

}


//mbelian
#ifdef HAVE_NET_DEVICE_OPS
static const struct net_device_ops ftnet_ops =
{
.ndo_open = &ft1000_open,
.ndo_stop = &ft1000_close,
.ndo_start_xmit = &ft1000_start_xmit,
.ndo_get_stats = &ft1000_netdev_stats,
};
#endif


//---------------------------------------------------------------------------
// Function:    init_ft1000_netdev
//
// Parameters:  ft1000dev  - device structure
//
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function initialize the network device
//
// Notes:
//
//---------------------------------------------------------------------------
u16 init_ft1000_netdev(struct ft1000_device *ft1000dev)
{
    struct net_device *netdev;
    FT1000_INFO *pInfo = NULL;
    PDPRAM_BLK pdpram_blk;
    int i;

	gCardIndex=0; //mbelian

    DEBUG("Enter init_ft1000_netdev...\n");


    netdev = alloc_etherdev( sizeof(FT1000_INFO));
    if (!netdev )
    {
        DEBUG("init_ft1000_netdev: can not allocate network device\n");
        return STATUS_FAILURE;
    }

    //pInfo = (PFT1000_INFO)netdev->priv;
	pInfo = (FT1000_INFO *) netdev_priv (netdev);

    //DEBUG("init_ft1000_netdev: gFt1000Info=%x, netdev=%x, ft1000dev=%x\n", gFt1000Info, netdev, ft1000dev);

    memset (pInfo, 0, sizeof(FT1000_INFO));

    dev_alloc_name(netdev, netdev->name);

    //for the first inserted card, decide the card index beginning number, in case there are existing network interfaces
    if ( gCardIndex == 0 )
    {
        DEBUG("init_ft1000_netdev: network device name is %s\n", netdev->name);

        if ( strncmp(netdev->name,"eth", 3) == 0) {
            //pInfo->CardNumber = atoi(&netdev->name[3]);
            gCardIndex = atoi(&netdev->name[3]);
            pInfo->CardNumber = gCardIndex;
            DEBUG("card number = %d\n", pInfo->CardNumber);
        }
        else {
            printk(KERN_ERR "ft1000: Invalid device name\n");
            kfree(netdev);
            return STATUS_FAILURE;
        }
    }
    else
    {
        //not the first inserted card, increase card number by 1
        /*gCardIndex ++;*/
        pInfo->CardNumber = gCardIndex;
        /*DEBUG("card number = %d\n", pInfo->CardNumber);*/ //mbelian
    }

    memset(&pInfo->stats, 0, sizeof(struct net_device_stats) );

   spin_lock_init(&pInfo->dpram_lock);
    pInfo->pFt1000Dev = ft1000dev;
    pInfo->DrvErrNum = 0;
    pInfo->ASICResetNum = 0;
    pInfo->registered = 1;
    pInfo->ft1000_reset = ft1000_reset;
    pInfo->mediastate = 0;
    pInfo->fifo_cnt = 0;
    pInfo->DeviceCreated = FALSE;
    pInfo->DeviceMajor = 0;
    pInfo->CurrentInterruptEnableMask = ISR_DEFAULT_MASK;
    pInfo->InterruptsEnabled = FALSE;
    pInfo->CardReady = 0;
    pInfo->DSP_loading = 0;
    pInfo->DSP_TIME[0] = 0;
    pInfo->DSP_TIME[1] = 0;
    pInfo->DSP_TIME[2] = 0;
    pInfo->DSP_TIME[3] = 0;
    pInfo->fAppMsgPend = 0;
    pInfo->fCondResetPend = 0;
	pInfo->usbboot = 0;
	pInfo->dspalive = 0;
	for (i=0;i<32 ;i++ )
	{
		pInfo->tempbuf[i] = 0;
	}

    INIT_LIST_HEAD(&pInfo->prov_list);

//mbelian
#ifdef HAVE_NET_DEVICE_OPS
	netdev->netdev_ops = &ftnet_ops;
#else
    netdev->hard_start_xmit = &ft1000_start_xmit;
    netdev->get_stats = &ft1000_netdev_stats;
    netdev->open = &ft1000_open;
    netdev->stop = &ft1000_close;
#endif

    //netif_stop_queue(netdev); //mbelian


    ft1000dev->net = netdev;



//init free_buff_lock, freercvpool, numofmsgbuf, pdpram_blk
//only init once per card
//Jim
    	  DEBUG("Initialize free_buff_lock and freercvpool\n");
        spin_lock_init(&free_buff_lock);

        // initialize a list of buffers to be use for queuing up receive command data
        INIT_LIST_HEAD (&freercvpool);

        // create list of free buffers
        for (i=0; i<NUM_OF_FREE_BUFFERS; i++) {
            // Get memory for DPRAM_DATA link list
            pdpram_blk = kmalloc ( sizeof(DPRAM_BLK), GFP_KERNEL );
            // Get a block of memory to store command data
            pdpram_blk->pbuffer = kmalloc ( MAX_CMD_SQSIZE, GFP_KERNEL );
            // link provisioning data
            list_add_tail (&pdpram_blk->list, &freercvpool);
        }
        numofmsgbuf = NUM_OF_FREE_BUFFERS;


    return STATUS_SUCCESS;

}



//---------------------------------------------------------------------------
// Function:    reg_ft1000_netdev
//
// Parameters:  ft1000dev  - device structure
//
//
// Returns:     STATUS_SUCCESS - success
//              STATUS_FAILURE - failure
//
// Description: This function register the network driver
//
// Notes:
//
//---------------------------------------------------------------------------
u16 reg_ft1000_netdev(struct ft1000_device *ft1000dev, struct usb_interface *intf)
{
    struct net_device *netdev;
    FT1000_INFO *pInfo;
    int i, rc;

    netdev = ft1000dev->net;
    pInfo = netdev_priv(ft1000dev->net);
    DEBUG("Enter reg_ft1000_netdev...\n");


    ft1000_read_register(ft1000dev, &pInfo->AsicID, FT1000_REG_ASIC_ID);

    usb_set_intfdata(intf, pInfo);
    SET_NETDEV_DEV(netdev, &intf->dev);

    rc = register_netdev(netdev);
    if (rc)
    {
        DEBUG("reg_ft1000_netdev: could not register network device\n");
        free_netdev(netdev);
        return STATUS_FAILURE;
    }


    //Create character device, implemented by Jim
    ft1000_CreateDevice(ft1000dev);

    //INIT_LIST_HEAD(&pInfo->prov_list);

    for (i=0; i<MAX_NUM_CARDS; i++) {
        poll_timer[i].function = ft1000_hbchk;
    }


    //hard code MAC address for now
/**
    netdev->dev_addr[0] = 0;
    netdev->dev_addr[1] = 7;
    netdev->dev_addr[2] = 0x35;
    netdev->dev_addr[3] = 0x84;
    netdev->dev_addr[4] = 0;
    netdev->dev_addr[5] = 0x20 + pInfo->CardNumber;
**/

    DEBUG ("reg_ft1000_netdev returned\n");

    pInfo->CardReady = 1;


   return STATUS_SUCCESS;
}

static int ft1000_reset(struct net_device *dev)
{
    ft1000_reset_card(dev);
    return 0;
}

//---------------------------------------------------------------------------
// Function:    ft1000_usb_transmit_complete
//
// Parameters:  urb  - transmitted usb urb
//
//
// Returns:     none
//
// Description: This is the callback function when a urb is transmitted
//
// Notes:
//
//---------------------------------------------------------------------------
static void ft1000_usb_transmit_complete(struct urb *urb)
{

    struct ft1000_device *ft1000dev = urb->context;

    //DEBUG("ft1000_usb_transmit_complete entered\n");
// Jim   spin_lock(&ft1000dev->device_lock);

    if (urb->status)
        printk("%s: TX status %d\n", ft1000dev->net->name, urb->status);

    netif_wake_queue(ft1000dev->net);

//Jim    spin_unlock(&ft1000dev->device_lock);
    //DEBUG("Return from ft1000_usb_transmit_complete\n");
}


/****************************************************************
 *     ft1000_control
 ****************************************************************/
static int ft1000_read_fifo_reg(struct ft1000_device *ft1000dev,unsigned int pipe,
                          u8 request,
                          u8 requesttype,
                          u16 value,
                          u16 index,
                          void *data,
                          u16 size,
                          int timeout)
{
    u16 ret;

    DECLARE_WAITQUEUE(wait, current);
    struct urb *urb;
    struct usb_ctrlrequest *dr;
    int status;

    if (ft1000dev == NULL )
    {
        DEBUG("NULL ft1000dev, failure\n");
        return STATUS_FAILURE;
    }
    else if ( ft1000dev->dev == NULL )
    {
        DEBUG("NULL ft1000dev->dev, failure\n");
        return STATUS_FAILURE;
    }

    spin_lock(&ft1000dev->device_lock);

    /*DECLARE_WAITQUEUE(wait, current);
    struct urb *urb;
    struct usb_ctrlrequest *dr;
    int status;*/

    if(in_interrupt())
    {
        spin_unlock(&ft1000dev->device_lock);
        return -EBUSY;
    }

    urb = usb_alloc_urb(0, GFP_KERNEL);
    dr = kmalloc(sizeof(struct usb_ctrlrequest), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);

    if(!urb || !dr)
    {
        if(urb) kfree(urb);
        spin_unlock(&ft1000dev->device_lock);
        return -ENOMEM;
    }



    dr->bRequestType = requesttype;
    dr->bRequest = request;
    dr->wValue = value;
    dr->wIndex = index;
    dr->wLength = size;

    usb_fill_control_urb(urb, ft1000dev->dev, pipe, (char*)dr, (void*)data, size, (void *)ft1000_control_complete, (void*)ft1000dev);


    init_waitqueue_head(&ft1000dev->control_wait);

    //current->state = TASK_INTERRUPTIBLE; //mbelian
	set_current_state(TASK_INTERRUPTIBLE);

    add_wait_queue(&ft1000dev->control_wait, &wait);




    status = usb_submit_urb(urb, GFP_KERNEL);

    if(status)
    {
        usb_free_urb(urb);
        kfree(dr);
        remove_wait_queue(&ft1000dev->control_wait, &wait);
        spin_unlock(&ft1000dev->device_lock);
        return status;
    }

    if(urb->status == -EINPROGRESS)
    {
        while(timeout && urb->status == -EINPROGRESS)
        {
            status = timeout = schedule_timeout(timeout);
        }
    }
    else
    {
        status = 1;
    }

    remove_wait_queue(&ft1000dev->control_wait, &wait);

    if(!status)
    {
        usb_unlink_urb(urb);
        printk("ft1000 timeout\n");
        status = -ETIMEDOUT;
    }
    else
    {
        status = urb->status;

        if(urb->status)
        {
            printk("ft1000 control message failed (urb addr: %p) with error number: %i\n", urb, (int)status);

            usb_clear_halt(ft1000dev->dev, usb_rcvctrlpipe(ft1000dev->dev, 0));
            usb_clear_halt(ft1000dev->dev, usb_sndctrlpipe(ft1000dev->dev, 0));
            usb_unlink_urb(urb);
        }
    }



    usb_free_urb(urb);
    kfree(dr);
    spin_unlock(&ft1000dev->device_lock);
    return ret;


}

//---------------------------------------------------------------------------
// Function:    ft1000_read_fifo_len
//
// Parameters:  ft1000dev - device structure
//
//
// Returns:     none
//
// Description: read the fifo length register content
//
// Notes:
//
//---------------------------------------------------------------------------
static inline u16 ft1000_read_fifo_len (struct net_device *dev)
{
    u16 temp;
    u16 ret;

    //FT1000_INFO *info = (PFT1000_INFO)dev->priv;
	FT1000_INFO *info = (FT1000_INFO *) netdev_priv (dev);
    struct ft1000_device *ft1000dev = info->pFt1000Dev;
//    DEBUG("ft1000_read_fifo_len: enter ft1000dev %x\n", ft1000dev);			//aelias [-] reason: warning: format ???%x??? expects type ???unsigned int???, but argument 2 has type ???struct ft1000_device *???
    DEBUG("ft1000_read_fifo_len: enter ft1000dev %p\n", ft1000dev);	//aelias [+] reason: up
    //ft1000_read_register(ft1000dev, &temp, FT1000_REG_MAG_UFSR);

    ret = STATUS_SUCCESS;

    ret = ft1000_read_fifo_reg(ft1000dev,
                          usb_rcvctrlpipe(ft1000dev->dev,0),
                          HARLEY_READ_REGISTER,
                          HARLEY_READ_OPERATION,
                          0,
                          FT1000_REG_MAG_UFSR,
                          &temp,
                          2,
                          LARGE_TIMEOUT);

    if (ret>0)
        ret = STATUS_SUCCESS;
    else
        ret = STATUS_FAILURE;

    DEBUG("ft1000_read_fifo_len: returned %d\n", temp);

    return (temp- 16);

}


//---------------------------------------------------------------------------
//
// Function:   ft1000_copy_down_pkt
// Descripton: This function will take an ethernet packet and convert it to
//             a Flarion packet prior to sending it to the ASIC Downlink
//             FIFO.
// Input:
//     dev    - device structure
//     packet - address of ethernet packet
//     len    - length of IP packet
// Output:
//     status - FAILURE
//              SUCCESS
//
//---------------------------------------------------------------------------
static int ft1000_copy_down_pkt (struct net_device *netdev, u8 *packet, u16 len)
{
    FT1000_INFO *pInfo = netdev_priv(netdev);
    struct ft1000_device *pFt1000Dev = pInfo->pFt1000Dev;


    int i, count, ret;
    USHORT *pTemp;
    USHORT checksum;
    u8 *t;

    if (!pInfo->CardReady)
    {

        DEBUG("ft1000_copy_down_pkt::Card Not Ready\n");
    	return STATUS_FAILURE;

    }


    //DEBUG("ft1000_copy_down_pkt() entered, len = %d\n", len);

#if 0
    // Check if there is room on the FIFO
    if ( len > ft1000_read_fifo_len (netdev) )
    {
         udelay(10);
         if ( len > ft1000_read_fifo_len (netdev) )
         {
             udelay(20);
         }

         if ( len > ft1000_read_fifo_len (netdev) )
         {
             udelay(20);
         }

         if ( len > ft1000_read_fifo_len (netdev) )
         {
             udelay(20);
         }

         if ( len > ft1000_read_fifo_len (netdev) )
         {
             udelay(20);
         }

         if ( len > ft1000_read_fifo_len (netdev) )
         {
             udelay(20);
         }

         if ( len > ft1000_read_fifo_len (netdev) )
         {
            DEBUG("ft1000_hw:ft1000_copy_down_pkt:Transmit FIFO is fulli - pkt drop\n");
            pInfo->stats.tx_errors++;
            return STATUS_SUCCESS;
         }
    }
#endif

    count = sizeof (PSEUDO_HDR) + len;
    if(count > MAX_BUF_SIZE)
    {
        DEBUG("Error:ft1000_copy_down_pkt:Message Size Overflow!\n");
    	DEBUG("size = %d\n", count);
    	return STATUS_FAILURE;
    }

    if ( count % 4)
        count = count + (4- (count %4) );

    pTemp = (PUSHORT)&(pFt1000Dev->tx_buf[0]);
    *pTemp ++ = ntohs(count);
    *pTemp ++ = 0x1020;
    *pTemp ++ = 0x2010;
    *pTemp ++ = 0x9100;
    *pTemp ++ = 0;
    *pTemp ++ = 0;
    *pTemp ++ = 0;
    pTemp = (PUSHORT)&(pFt1000Dev->tx_buf[0]);
    checksum = *pTemp ++;
    for (i=1; i<7; i++)
    {
        checksum ^= *pTemp ++;
    }
    *pTemp++ = checksum;
    memcpy (&(pFt1000Dev->tx_buf[sizeof(PSEUDO_HDR)]), packet, len);

    //usb_init_urb(pFt1000Dev->tx_urb); //mbelian

    netif_stop_queue(netdev);

    //DEBUG ("ft1000_copy_down_pkt: count = %d\n", count);

    usb_fill_bulk_urb(pFt1000Dev->tx_urb,
                      pFt1000Dev->dev,
                      usb_sndbulkpipe(pFt1000Dev->dev, pFt1000Dev->bulk_out_endpointAddr),
                      pFt1000Dev->tx_buf,
                      count,
                      ft1000_usb_transmit_complete,
                      (void*)pFt1000Dev);

    t = (u8 *)pFt1000Dev->tx_urb->transfer_buffer;
    //DEBUG("transfer_length=%d\n", pFt1000Dev->tx_urb->transfer_buffer_length);
    /*for (i=0; i<count; i++ )
    {
       DEBUG("%x    ", *t++ );
    }*/


    ret = usb_submit_urb(pFt1000Dev->tx_urb, GFP_ATOMIC);
    if(ret)
    {
		DEBUG("ft1000 failed tx_urb %d\n", ret);

   /*     pInfo->stats.tx_errors++;

        netif_start_queue(netdev);  */  //mbelian
		return STATUS_FAILURE;

    }
    else
    {
        //DEBUG("ft1000 sucess tx_urb %d\n", ret);

        pInfo->stats.tx_packets++;
        pInfo->stats.tx_bytes += (len+14);
    }

    //DEBUG("ft1000_copy_down_pkt() exit\n");

    return STATUS_SUCCESS;
}

//---------------------------------------------------------------------------
// Function:    ft1000_start_xmit
//
// Parameters:  skb - socket buffer to be sent
//              dev - network device
//
//
// Returns:     none
//
// Description: transmit a ethernet packet
//
// Notes:
//
//---------------------------------------------------------------------------
static int ft1000_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    FT1000_INFO *pInfo = netdev_priv(dev);
    struct ft1000_device *pFt1000Dev= pInfo->pFt1000Dev;
    u8 *pdata;
    int maxlen, pipe;


    //DEBUG(" ft1000_start_xmit() entered\n");

    if ( skb == NULL )
    {
        DEBUG ("ft1000_hw: ft1000_start_xmit:skb == NULL!!!\n" );
        return STATUS_FAILURE;
    }

    if ( pFt1000Dev->status & FT1000_STATUS_CLOSING)
    {
        DEBUG("network driver is closed, return\n");
        dev_kfree_skb(skb);
        //usb_kill_urb(pFt1000Dev->tx_urb); //mbelian
        return STATUS_SUCCESS;
    }

    //DEBUG("ft1000_start_xmit 1:length of packet = %d\n", skb->len);
    pipe = usb_sndbulkpipe(pFt1000Dev->dev, pFt1000Dev->bulk_out_endpointAddr);
    maxlen = usb_maxpacket(pFt1000Dev->dev, pipe, usb_pipeout(pipe));
    //DEBUG("ft1000_start_xmit 2: pipe=%d dev->maxpacket  = %d\n", pipe, maxlen);

    pdata = (u8 *)skb->data;
    /*for (i=0; i<skb->len; i++)
        DEBUG("skb->data[%d]=%x    ", i, *(skb->data+i));

    DEBUG("\n");*/


    if (pInfo->mediastate == 0)
    {
        /* Drop packet is mediastate is down */
        DEBUG("ft1000_hw:ft1000_start_xmit:mediastate is down\n");
        dev_kfree_skb(skb);
        return STATUS_SUCCESS;
    }

    if ( (skb->len < ENET_HEADER_SIZE) || (skb->len > ENET_MAX_SIZE) )
    {
        /* Drop packet which has invalid size */
        DEBUG("ft1000_hw:ft1000_start_xmit:invalid ethernet length\n");
        dev_kfree_skb(skb);
        return STATUS_SUCCESS;
    }
//mbelian
    if(ft1000_copy_down_pkt (dev, (pdata+ENET_HEADER_SIZE-2), skb->len - ENET_HEADER_SIZE + 2) == STATUS_FAILURE)
	{
    	dev_kfree_skb(skb);
		return STATUS_SUCCESS;
	}

    dev_kfree_skb(skb);
    //DEBUG(" ft1000_start_xmit() exit\n");

    return 0;
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_copy_up_pkt
// Descripton: This function will take a packet from the FIFO up link and
//             convert it into an ethernet packet and deliver it to the IP stack
// Input:
//     urb - the receving usb urb
//
// Output:
//     status - FAILURE
//              SUCCESS
//
//---------------------------------------------------------------------------
static int ft1000_copy_up_pkt (struct urb *urb)
{
    PFT1000_INFO info = urb->context;
    struct ft1000_device *ft1000dev = info->pFt1000Dev;
    struct net_device *net = ft1000dev->net;

    u16 tempword;
    u16 len;
    u16 lena; //mbelian
    struct sk_buff *skb;
    u16 i;
    u8 *pbuffer=NULL;
    u8 *ptemp=NULL;
    u16 *chksum;


    //DEBUG("ft1000_copy_up_pkt entered\n");

    if ( ft1000dev->status & FT1000_STATUS_CLOSING)
    {
        DEBUG("network driver is closed, return\n");
        return STATUS_SUCCESS;
    }

    // Read length
    len = urb->transfer_buffer_length;
    lena = urb->actual_length; //mbelian
    //DEBUG("ft1000_copy_up_pkt: transfer_buffer_length=%d, actual_buffer_len=%d\n",
      //       urb->transfer_buffer_length, urb->actual_length);

    chksum = (PUSHORT)ft1000dev->rx_buf;

    tempword = *chksum++;
    for (i=1; i<7; i++)
    {
        tempword ^= *chksum++;
    }

    if  (tempword != *chksum)
    {
        info->stats.rx_errors ++;
        ft1000_submit_rx_urb(info);
        return STATUS_FAILURE;
    }


    //DEBUG("ft1000_copy_up_pkt: checksum is correct %x\n", *chksum);

    skb = dev_alloc_skb(len+12+2);

    if (skb == NULL)
    {
        DEBUG("ft1000_copy_up_pkt: No Network buffers available\n");
        info->stats.rx_errors++;
        ft1000_submit_rx_urb(info);
        return STATUS_FAILURE;
    }

    pbuffer = (u8 *)skb_put(skb, len+12);

    //subtract the number of bytes read already
    ptemp = pbuffer;

    // fake MAC address
    *pbuffer++ = net->dev_addr[0];
    *pbuffer++ = net->dev_addr[1];
    *pbuffer++ = net->dev_addr[2];
    *pbuffer++ = net->dev_addr[3];
    *pbuffer++ = net->dev_addr[4];
    *pbuffer++ = net->dev_addr[5];
    *pbuffer++ = 0x00;
    *pbuffer++ = 0x07;
    *pbuffer++ = 0x35;
    *pbuffer++ = 0xff;
    *pbuffer++ = 0xff;
    *pbuffer++ = 0xfe;




    memcpy(pbuffer, ft1000dev->rx_buf+sizeof(PSEUDO_HDR), len-sizeof(PSEUDO_HDR));

    //DEBUG("ft1000_copy_up_pkt: Data passed to Protocol layer\n");
    /*for (i=0; i<len+12; i++)
    {
        DEBUG("ft1000_copy_up_pkt: Protocol Data: 0x%x\n ", *ptemp++);
    }*/

    skb->dev = net;

    skb->protocol = eth_type_trans(skb, net);
    skb->ip_summed = CHECKSUM_UNNECESSARY;
    netif_rx(skb);

    info->stats.rx_packets++;
    // Add on 12 bytes for MAC address which was removed
    info->stats.rx_bytes += (lena+12); //mbelian

    ft1000_submit_rx_urb(info);
    //DEBUG("ft1000_copy_up_pkt exited\n");
    return SUCCESS;
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_submit_rx_urb
// Descripton: the receiving function of the network driver
//
// Input:
//     info - a private structure contains the device information
//
// Output:
//     status - FAILURE
//              SUCCESS
//
//---------------------------------------------------------------------------
static int ft1000_submit_rx_urb(PFT1000_INFO info)
{
    int result;
    struct ft1000_device *pFt1000Dev = info->pFt1000Dev;

    //netif_carrier_on(pFt1000Dev->net);

    //DEBUG ("ft1000_submit_rx_urb entered: sizeof rx_urb is %d\n", sizeof(*pFt1000Dev->rx_urb));
    if ( pFt1000Dev->status & FT1000_STATUS_CLOSING)
    {
        DEBUG("network driver is closed, return\n");
        //usb_kill_urb(pFt1000Dev->rx_urb); //mbelian
        return STATUS_SUCCESS;
    }
    //memset(pFt1000Dev->rx_urb, 0, sizeof(*pFt1000Dev->rx_urb));
    //usb_init_urb(pFt1000Dev->rx_urb);//mbelian

    //spin_lock_init(&pFt1000Dev->rx_urb->lock);

    usb_fill_bulk_urb(pFt1000Dev->rx_urb,
            pFt1000Dev->dev,
            usb_rcvbulkpipe(pFt1000Dev->dev, pFt1000Dev->bulk_in_endpointAddr),
            pFt1000Dev->rx_buf,
            MAX_BUF_SIZE,
            (usb_complete_t)ft1000_copy_up_pkt,
            info);


    if((result = usb_submit_urb(pFt1000Dev->rx_urb, GFP_ATOMIC)))
    {
        printk("ft1000_submit_rx_urb: submitting rx_urb %d failed\n", result);
        return STATUS_FAILURE;
    }

    //DEBUG("ft1000_submit_rx_urb exit: result=%d\n", result);

    return STATUS_SUCCESS;
}

//---------------------------------------------------------------------------
// Function:    ft1000_open
//
// Parameters:
//              dev - network device
//
//
// Returns:     none
//
// Description: open the network driver
//
// Notes:
//
//---------------------------------------------------------------------------
static int ft1000_open (struct net_device *dev)
{
	FT1000_INFO *pInfo = (FT1000_INFO *)netdev_priv(dev);
    struct timeval tv; //mbelian

    DEBUG("ft1000_open is called for card %d\n", pInfo->CardNumber);
    //DEBUG("ft1000_open: dev->addr=%x, dev->addr_len=%d\n", dev->addr, dev->addr_len);

	pInfo->stats.rx_bytes = 0; //mbelian
	pInfo->stats.tx_bytes = 0; //mbelian
	pInfo->stats.rx_packets = 0; //mbelian
	pInfo->stats.tx_packets = 0; //mbelian
	do_gettimeofday(&tv);
    pInfo->ConTm = tv.tv_sec;
	pInfo->ProgConStat = 0; //mbelian


    netif_start_queue(dev);

    //netif_device_attach(dev);

    netif_carrier_on(dev); //mbelian

    ft1000_submit_rx_urb(pInfo);
    return 0;
}

//---------------------------------------------------------------------------
// Function:    ft1000_close
//
// Parameters:
//              net - network device
//
//
// Returns:     none
//
// Description: close the network driver
//
// Notes:
//
//---------------------------------------------------------------------------
int ft1000_close(struct net_device *net)
{
	FT1000_INFO *pInfo = (FT1000_INFO *) netdev_priv (net);
    struct ft1000_device *ft1000dev = pInfo->pFt1000Dev;

    //DEBUG ("ft1000_close: netdev->refcnt=%d\n", net->refcnt);

    ft1000dev->status |= FT1000_STATUS_CLOSING;

    //DEBUG("ft1000_close: calling usb_kill_urb \n");
    //usb_kill_urb(ft1000dev->rx_urb);
    //usb_kill_urb(ft1000dev->tx_urb);


    DEBUG("ft1000_close: pInfo=%p, ft1000dev=%p\n", pInfo, ft1000dev);
    netif_carrier_off(net);//mbelian
    netif_stop_queue(net);
    //DEBUG("ft1000_close: netif_stop_queue called\n");
    ft1000dev->status &= ~FT1000_STATUS_CLOSING;

   pInfo->ProgConStat = 0xff; //mbelian


    return 0;
}

static struct net_device_stats *ft1000_netdev_stats(struct net_device *dev)
{
	FT1000_INFO *info = (FT1000_INFO *) netdev_priv (dev);
    //struct ft1000_device *ft1000dev = info->pFt1000Dev;

    //return &(ft1000dev->stats);//mbelian
	return &(info->stats); //mbelian
}


/*********************************************************************************
Jim
*/


//---------------------------------------------------------------------------
//
// Function:   ft1000_chkcard
// Descripton: This function will check if the device is presently available on
//             the system.
// Input:
//     dev    - device structure
// Output:
//     status - FALSE (device is not present)
//              TRUE  (device is present)
//
//---------------------------------------------------------------------------
static int ft1000_chkcard (struct ft1000_device *dev) {
    u16 tempword;
    u16 status;
	FT1000_INFO *info = (FT1000_INFO *) netdev_priv (dev->net);

    if (info->fCondResetPend)
    {
        DEBUG("ft1000_hw:ft1000_chkcard:Card is being reset, return FALSE\n");
        return TRUE;
    }

    // Mask register is used to check for device presence since it is never
    // set to zero.
    status = ft1000_read_register(dev, &tempword, FT1000_REG_SUP_IMASK);
    //DEBUG("ft1000_hw:ft1000_chkcard: read FT1000_REG_SUP_IMASK = %x\n", tempword);
    if (tempword == 0) {
        DEBUG("ft1000_hw:ft1000_chkcard: IMASK = 0 Card not detected\n");
        return FALSE;
    }

    // The system will return the value of 0xffff for the version register
    // if the device is not present.
    status = ft1000_read_register(dev, &tempword, FT1000_REG_ASIC_ID);
    //DEBUG("ft1000_hw:ft1000_chkcard: read FT1000_REG_ASIC_ID = %x\n", tempword);
    //pxu if (tempword == 0xffff) {
    if (tempword != 0x1b01 ){
	dev->status |= FT1000_STATUS_CLOSING; //mbelian
        DEBUG("ft1000_hw:ft1000_chkcard: Version = 0xffff Card not detected\n");
        return FALSE;
    }
    return TRUE;
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_hbchk
// Descripton: This function will perform the heart beat check of the DSP as
//             well as the ASIC.
// Input:
//     dev    - device structure
// Output:
//     none
//
//---------------------------------------------------------------------------
static void ft1000_hbchk(u_long data)
{
    struct ft1000_device *dev = (struct ft1000_device *)data;

    FT1000_INFO *info;
    USHORT tempword;
        u16 status;
	info = (FT1000_INFO *) netdev_priv (dev->net);

    DEBUG("ft1000_hbchk called for CardNumber = %d CardReady = %d\n", info->CardNumber, info->CardReady);

    if (info->fCondResetPend == 1) {
        // Reset ASIC and DSP
        status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER0, (PUCHAR)&(info->DSP_TIME[0]), FT1000_MAG_DSP_TIMER0_INDX);
        status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER1, (PUCHAR)&(info->DSP_TIME[1]), FT1000_MAG_DSP_TIMER1_INDX);
        status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER2, (PUCHAR)&(info->DSP_TIME[2]), FT1000_MAG_DSP_TIMER2_INDX);
        status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER3, (PUCHAR)&(info->DSP_TIME[3]), FT1000_MAG_DSP_TIMER3_INDX);

        info->DrvErrNum = DSP_CONDRESET_INFO;
        DEBUG("ft1000_hw:DSP conditional reset requested\n");
        ft1000_reset_card(dev->net);
        info->fCondResetPend = 0;
        /* Schedule this module to run every 2 seconds */

        poll_timer[info->CardNumber].expires = jiffies + (2*HZ);
        poll_timer[info->CardNumber].data = (u_long)dev;
        add_timer(&poll_timer[info->CardNumber]);



        return;
    }

    if (info->CardReady == 1) {
        // Perform dsp heartbeat check
            status = ntohs(ft1000_read_dpram16(dev, FT1000_MAG_HI_HO, (PUCHAR)&tempword, FT1000_MAG_HI_HO_INDX));
        DEBUG("ft1000_hw:ft1000_hbchk:hi_ho value = 0x%x\n", tempword);
        // Let's perform another check if ho is not detected
        if (tempword != ho) {
              status  = ntohs(ft1000_read_dpram16(dev, FT1000_MAG_HI_HO, (PUCHAR)&tempword,FT1000_MAG_HI_HO_INDX));
        }
        if (tempword != ho) {
            printk(KERN_INFO "ft1000: heartbeat failed - no ho detected\n");
                status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER0, (PUCHAR)&(info->DSP_TIME[0]), FT1000_MAG_DSP_TIMER0_INDX);
                status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER1, (PUCHAR)&(info->DSP_TIME[1]), FT1000_MAG_DSP_TIMER1_INDX);
                status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER2, (PUCHAR)&(info->DSP_TIME[2]), FT1000_MAG_DSP_TIMER2_INDX);
                status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER3, (PUCHAR)&(info->DSP_TIME[3]), FT1000_MAG_DSP_TIMER3_INDX);
            info->DrvErrNum = DSP_HB_INFO;
            if (ft1000_reset_card(dev->net) == 0) {
               printk(KERN_INFO "ft1000: Hardware Failure Detected - PC Card disabled\n");
               info->ProgConStat = 0xff;
               return;
            }
            /* Schedule this module to run every 2 seconds */
            poll_timer[info->CardNumber].expires = jiffies + (2*HZ);
            poll_timer[info->CardNumber].data = (u_long)dev;
            add_timer(&poll_timer[info->CardNumber]);
            return;
        }

        status = ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
        // Let's check doorbell again if fail
        if (tempword & FT1000_DB_HB) {
                status = ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
        }
        if (tempword & FT1000_DB_HB) {
            printk(KERN_INFO "ft1000: heartbeat doorbell not clear by firmware\n");
            status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER0, (PUCHAR)&(info->DSP_TIME[0]), FT1000_MAG_DSP_TIMER0_INDX);
            status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER1, (PUCHAR)&(info->DSP_TIME[1]), FT1000_MAG_DSP_TIMER1_INDX);
            status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER2, (PUCHAR)&(info->DSP_TIME[2]), FT1000_MAG_DSP_TIMER2_INDX);
            status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER3, (PUCHAR)&(info->DSP_TIME[3]), FT1000_MAG_DSP_TIMER3_INDX);
            info->DrvErrNum = DSP_HB_INFO;
            if (ft1000_reset_card(dev->net) == 0) {
               printk(KERN_INFO "ft1000: Hardware Failure Detected - PC Card disabled\n");
               info->ProgConStat = 0xff;
               return;
            }
            /* Schedule this module to run every 2 seconds */
            poll_timer[info->CardNumber].expires = jiffies + (2*HZ);
            poll_timer[info->CardNumber].data = (u_long)dev;
            add_timer(&poll_timer[info->CardNumber]);
            return;
        }

        // Set dedicated area to hi and ring appropriate doorbell according
        // to hi/ho heartbeat protocol
        ft1000_write_dpram16(dev, FT1000_MAG_HI_HO, hi_mag, FT1000_MAG_HI_HO_INDX);

        status = ntohs(ft1000_read_dpram16(dev, FT1000_MAG_HI_HO, (PUCHAR)&tempword, FT1000_MAG_HI_HO_INDX));
        // Let's write hi again if fail
        if (tempword != hi) {
               ft1000_write_dpram16(dev, FT1000_MAG_HI_HO, hi_mag, FT1000_MAG_HI_HO_INDX);
                   status = ntohs(ft1000_read_dpram16(dev, FT1000_MAG_HI_HO, (PUCHAR)&tempword, FT1000_MAG_HI_HO_INDX));

        }
        if (tempword != hi) {
            printk(KERN_INFO "ft1000: heartbeat failed - cannot write hi into DPRAM\n");
            status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER0, (PUCHAR)&(info->DSP_TIME[0]), FT1000_MAG_DSP_TIMER0_INDX);
            status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER1, (PUCHAR)&(info->DSP_TIME[1]), FT1000_MAG_DSP_TIMER1_INDX);
            status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER2, (PUCHAR)&(info->DSP_TIME[2]), FT1000_MAG_DSP_TIMER2_INDX);
            status = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER3, (PUCHAR)&(info->DSP_TIME[3]), FT1000_MAG_DSP_TIMER3_INDX);

            info->DrvErrNum = DSP_HB_INFO;
            if (ft1000_reset_card(dev->net) == 0) {
               printk(KERN_INFO "ft1000: Hardware Failure Detected - PC Card disabled\n");
               info->ProgConStat = 0xff;
               return;
            }
            /* Schedule this module to run every 2 seconds */
            poll_timer[info->CardNumber].expires = jiffies + (2*HZ);
            poll_timer[info->CardNumber].data = (u_long)dev;
            add_timer(&poll_timer[info->CardNumber]);
            return;
        }
        ft1000_write_register(dev, FT1000_DB_HB, FT1000_REG_DOORBELL);

    }

    /* Schedule this module to run every 2 seconds */
    poll_timer[info->CardNumber].expires = jiffies + (2*HZ);
    poll_timer[info->CardNumber].data = (u_long)dev;
    add_timer(&poll_timer[info->CardNumber]);
}

//---------------------------------------------------------------------------
//
// Function:   ft1000_receive_cmd
// Descripton: This function will read a message from the dpram area.
// Input:
//    dev - network device structure
//    pbuffer - caller supply address to buffer
//    pnxtph - pointer to next pseudo header
// Output:
//   Status = 0 (unsuccessful)
//          = 1 (successful)
//
//---------------------------------------------------------------------------
static BOOLEAN ft1000_receive_cmd (struct ft1000_device *dev, u16 *pbuffer, int maxsz, u16 *pnxtph) {
    u16 size, ret;
    u16 *ppseudohdr;
    int i;
    u16 tempword;

    ret = ft1000_read_dpram16(dev, FT1000_MAG_PH_LEN, (PUCHAR)&size, FT1000_MAG_PH_LEN_INDX);
    size = ntohs(size) + PSEUDOSZ;
    if (size > maxsz) {
        DEBUG("FT1000:ft1000_receive_cmd:Invalid command length = %d\n", size);
        return FALSE;
    }
    else {
        ppseudohdr = (u16 *)pbuffer;
        //spin_lock_irqsave (&info->dpram_lock, flags);
        ft1000_write_register(dev, FT1000_DPRAM_MAG_RX_BASE, FT1000_REG_DPRAM_ADDR);
        ret = ft1000_read_register(dev, pbuffer, FT1000_REG_MAG_DPDATAH);
        //DEBUG("ft1000_hw:received data = 0x%x\n", *pbuffer);
        pbuffer++;
        ft1000_write_register(dev,  FT1000_DPRAM_MAG_RX_BASE+1, FT1000_REG_DPRAM_ADDR);
        for (i=0; i<=(size>>2); i++) {
            ret = ft1000_read_register(dev, pbuffer, FT1000_REG_MAG_DPDATAL);
            pbuffer++;
            ret = ft1000_read_register(dev, pbuffer, FT1000_REG_MAG_DPDATAH);
            pbuffer++;
        }
        //copy odd aligned word
        ret = ft1000_read_register(dev, pbuffer, FT1000_REG_MAG_DPDATAL);
        //DEBUG("ft1000_hw:received data = 0x%x\n", *pbuffer);
        pbuffer++;
        ret = ft1000_read_register(dev, pbuffer, FT1000_REG_MAG_DPDATAH);
        //DEBUG("ft1000_hw:received data = 0x%x\n", *pbuffer);
        pbuffer++;
        if (size & 0x0001) {
            //copy odd byte from fifo
            ret = ft1000_read_register(dev, &tempword, FT1000_REG_DPRAM_DATA);
            *pbuffer = ntohs(tempword);
        }
        //spin_unlock_irqrestore(&info->dpram_lock, flags);

        // Check if pseudo header checksum is good
        // Calculate pseudo header checksum
        tempword = *ppseudohdr++;
        for (i=1; i<7; i++) {
            tempword ^= *ppseudohdr++;
        }
        if ( (tempword != *ppseudohdr) ) {
            return FALSE;
        }


#if 0
        DEBUG("ft1000_receive_cmd:pbuffer\n");
        for(i = 0; i < size; i+=5)
        {
            if( (i + 5) < size )
                DEBUG("0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", tempbuffer[i], tempbuffer[i+1], tempbuffer[i+2], tempbuffer[i+3], tempbuffer[i+4]);
            else
            {
                for (j = i; j < size; j++)
                DEBUG("0x%x ", tempbuffer[j]);
                DEBUG("\n");
                break;
            }
        }

#endif

        return TRUE;
    }
}


static int ft1000_dsp_prov(void *arg)
{
    struct ft1000_device *dev = (struct ft1000_device *)arg;
	FT1000_INFO *info = (FT1000_INFO *) netdev_priv (dev->net);
    u16 tempword;
    u16 len;
    u16 i=0;
    PPROV_RECORD ptr;
    PPSEUDO_HDR ppseudo_hdr;
    PUSHORT pmsg;
    u16 status;
    USHORT TempShortBuf [256];

    DEBUG("*** DspProv Entered\n");

    while (         list_empty(&info->prov_list) == 0
                   /*&&  !kthread_should_stop()  */)
    {
	DEBUG("DSP Provisioning List Entry\n");

        // Check if doorbell is available
        DEBUG("check if doorbell is cleared\n");
        status = ft1000_read_register (dev, &tempword, FT1000_REG_DOORBELL);
        if (status)
	{
		DEBUG("ft1000_dsp_prov::ft1000_read_register error\n");
            break;
        }

        while (tempword & FT1000_DB_DPRAM_TX) {
            mdelay(10);
            i++;
            if (i==10) {
               DEBUG("FT1000:ft1000_dsp_prov:message drop\n");
               return STATUS_FAILURE;
            }
            ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
        }

        if ( !(tempword & FT1000_DB_DPRAM_TX) ) {
            DEBUG("*** Provision Data Sent to DSP\n");

            // Send provisioning data
            ptr = list_entry(info->prov_list.next, PROV_RECORD, list);
            len = *(u16 *)ptr->pprov_data;
            len = htons(len);
            len += PSEUDOSZ;
            //len = htons(len);

            pmsg = (PUSHORT)ptr->pprov_data;
            ppseudo_hdr = (PPSEUDO_HDR)pmsg;
            // Insert slow queue sequence number
            ppseudo_hdr->seq_num = info->squeseqnum++;
            ppseudo_hdr->portsrc = 0;
            // Calculate new checksum
            ppseudo_hdr->checksum = *pmsg++;
            //DEBUG("checksum = 0x%x\n", ppseudo_hdr->checksum);
            for (i=1; i<7; i++) {
                ppseudo_hdr->checksum ^= *pmsg++;
                //DEBUG("checksum = 0x%x\n", ppseudo_hdr->checksum);
            }

            TempShortBuf[0] = 0;
            TempShortBuf[1] = htons (len);
            memcpy(&TempShortBuf[2], ppseudo_hdr, len);

            status = ft1000_write_dpram32 (dev, 0, (PUCHAR)&TempShortBuf[0], (unsigned short)(len+2));
            status = ft1000_write_register (dev, FT1000_DB_DPRAM_TX, FT1000_REG_DOORBELL);

            list_del(&ptr->list);
            kfree(ptr->pprov_data);
            kfree(ptr);
        }
        msleep(10);
    }

    DEBUG("DSP Provisioning List Entry finished\n");

    msleep(100);

    info->fProvComplete = 1;
    info->CardReady = 1;
    info->DSP_loading= 0;
    return STATUS_SUCCESS;

}


static int ft1000_proc_drvmsg (struct ft1000_device *dev, u16 size) {
	FT1000_INFO *info = (FT1000_INFO *) netdev_priv (dev->net);
    u16 msgtype;
    u16 tempword;
    PMEDIAMSG pmediamsg;
    PDSPINITMSG pdspinitmsg;
    PDRVMSG pdrvmsg;
    u16 i;
    PPSEUDO_HDR ppseudo_hdr;
    PUSHORT pmsg;
    u16 status;
    //struct timeval tv; //mbelian
    union {
        u8  byte[2];
        u16 wrd;
    } convert;


    char *cmdbuffer = kmalloc(1600, GFP_KERNEL);
    if (!cmdbuffer)
	return STATUS_FAILURE;

    status = ft1000_read_dpram32(dev, 0x200, cmdbuffer, size);


    //if (ft1000_receive_cmd(dev, &cmdbuffer[0], MAX_CMD_SQSIZE, &tempword))
    {

#ifdef JDEBUG
        DEBUG("ft1000_proc_drvmsg:cmdbuffer\n");
        for(i = 0; i < size; i+=5)
        {
            if( (i + 5) < size )
                DEBUG("0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", cmdbuffer[i], cmdbuffer[i+1], cmdbuffer[i+2], cmdbuffer[i+3], cmdbuffer[i+4]);
            else
            {
                for (j = i; j < size; j++)
                DEBUG("0x%x ", cmdbuffer[j]);
                DEBUG("\n");
                break;
            }
        }
#endif
        pdrvmsg = (PDRVMSG)&cmdbuffer[2];
        msgtype = ntohs(pdrvmsg->type);
        DEBUG("ft1000_proc_drvmsg:Command message type = 0x%x\n", msgtype);
        switch (msgtype) {
            case MEDIA_STATE: {
                DEBUG("ft1000_proc_drvmsg:Command message type = MEDIA_STATE");

                pmediamsg = (PMEDIAMSG)&cmdbuffer[0];
                if (info->ProgConStat != 0xFF) {
                    if (pmediamsg->state) {
                        DEBUG("Media is up\n");
                        if (info->mediastate == 0) {
                            if ( info->NetDevRegDone )
                            {
                                //netif_carrier_on(dev->net);//mbelian
                                netif_wake_queue(dev->net);
                            }
                            info->mediastate = 1;
                            /*do_gettimeofday(&tv);
                            info->ConTm = tv.tv_sec;*/ //mbelian
                        }
                    }
                    else {
                        DEBUG("Media is down\n");
                        if (info->mediastate == 1) {
                            info->mediastate = 0;
                            if ( info->NetDevRegDone )
                            {
                                //netif_carrier_off(dev->net); mbelian
                                //netif_stop_queue(dev->net);
                            }
                            info->ConTm = 0;
                        }
                    }
                }
                else {
                    DEBUG("Media is down\n");
                    if (info->mediastate == 1) {
                        info->mediastate = 0;
                        if ( info->NetDevRegDone)
                        {
                            //netif_carrier_off(dev->net); //mbelian
                            //netif_stop_queue(dev->net);
                        }
                        info->ConTm = 0;
                    }
                }
                break;
            }
            case DSP_INIT_MSG: {
                DEBUG("ft1000_proc_drvmsg:Command message type = DSP_INIT_MSG");

                pdspinitmsg = (PDSPINITMSG)&cmdbuffer[2];
                memcpy(info->DspVer, pdspinitmsg->DspVer, DSPVERSZ);
                DEBUG("DSPVER = 0x%2x 0x%2x 0x%2x 0x%2x\n", info->DspVer[0], info->DspVer[1], info->DspVer[2], info->DspVer[3]);
                memcpy(info->HwSerNum, pdspinitmsg->HwSerNum, HWSERNUMSZ);
                memcpy(info->Sku, pdspinitmsg->Sku, SKUSZ);
                memcpy(info->eui64, pdspinitmsg->eui64, EUISZ);
                DEBUG("EUI64=%2x.%2x.%2x.%2x.%2x.%2x.%2x.%2x\n", info->eui64[0],info->eui64[1], info->eui64[2], info->eui64[3], info->eui64[4], info->eui64[5],info->eui64[6], info->eui64[7]);
                dev->net->dev_addr[0] = info->eui64[0];
                dev->net->dev_addr[1] = info->eui64[1];
                dev->net->dev_addr[2] = info->eui64[2];
                dev->net->dev_addr[3] = info->eui64[5];
                dev->net->dev_addr[4] = info->eui64[6];
                dev->net->dev_addr[5] = info->eui64[7];

                if (ntohs(pdspinitmsg->length) == (sizeof(DSPINITMSG) - 20) ) {
                    memcpy(info->ProductMode, pdspinitmsg->ProductMode, MODESZ);
                    memcpy(info->RfCalVer, pdspinitmsg->RfCalVer, CALVERSZ);
                    memcpy(info->RfCalDate, pdspinitmsg->RfCalDate, CALDATESZ);
                    DEBUG("RFCalVer = 0x%2x 0x%2x\n", info->RfCalVer[0], info->RfCalVer[1]);
                }
                break;
            }
            case DSP_PROVISION: {
                DEBUG("ft1000_proc_drvmsg:Command message type = DSP_PROVISION\n");

                // kick off dspprov routine to start provisioning
                // Send provisioning data to DSP
                if (list_empty(&info->prov_list) == 0)
                {
		    info->fProvComplete = 0;
		    status = ft1000_dsp_prov(dev);
		    if (status != STATUS_SUCCESS)
		        goto out;
                }
                else {
                    info->fProvComplete = 1;
                    status = ft1000_write_register (dev, FT1000_DB_HB, FT1000_REG_DOORBELL);
                    DEBUG("FT1000:drivermsg:No more DSP provisioning data in dsp image\n");
                }
                DEBUG("ft1000_proc_drvmsg:DSP PROVISION is done\n");
                break;
            }
            case DSP_STORE_INFO: {
                DEBUG("ft1000_proc_drvmsg:Command message type = DSP_STORE_INFO");

                DEBUG("FT1000:drivermsg:Got DSP_STORE_INFO\n");
                tempword = ntohs(pdrvmsg->length);
                info->DSPInfoBlklen = tempword;
                if (tempword < (MAX_DSP_SESS_REC-4) ) {
                    pmsg = (PUSHORT)&pdrvmsg->data[0];
                    for (i=0; i<((tempword+1)/2); i++) {
                        DEBUG("FT1000:drivermsg:dsp info data = 0x%x\n", *pmsg);
                        info->DSPInfoBlk[i+10] = *pmsg++;
                    }
                }
                else {
                    info->DSPInfoBlklen = 0;
                }
                break;
            }
            case DSP_GET_INFO: {
                DEBUG("FT1000:drivermsg:Got DSP_GET_INFO\n");
                // copy dsp info block to dsp
                info->DrvMsgPend = 1;
                // allow any outstanding ioctl to finish
                mdelay(10);
                status = ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
                if (tempword & FT1000_DB_DPRAM_TX) {
                    mdelay(10);
                    status = ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
                    if (tempword & FT1000_DB_DPRAM_TX) {
                        mdelay(10);
                            status = ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
                            if (tempword & FT1000_DB_DPRAM_TX) {
                                break;
                            }
                    }
                }

                // Put message into Slow Queue
                // Form Pseudo header
                pmsg = (PUSHORT)info->DSPInfoBlk;
                *pmsg++ = 0;
                *pmsg++ = htons(info->DSPInfoBlklen+20+info->DSPInfoBlklen);
                ppseudo_hdr = (PPSEUDO_HDR)(PUSHORT)&info->DSPInfoBlk[2];
                ppseudo_hdr->length = htons(info->DSPInfoBlklen+4+info->DSPInfoBlklen);
                ppseudo_hdr->source = 0x10;
                ppseudo_hdr->destination = 0x20;
                ppseudo_hdr->portdest = 0;
                ppseudo_hdr->portsrc = 0;
                ppseudo_hdr->sh_str_id = 0;
                ppseudo_hdr->control = 0;
                ppseudo_hdr->rsvd1 = 0;
                ppseudo_hdr->rsvd2 = 0;
                ppseudo_hdr->qos_class = 0;
                // Insert slow queue sequence number
                ppseudo_hdr->seq_num = info->squeseqnum++;
                // Insert application id
                ppseudo_hdr->portsrc = 0;
                // Calculate new checksum
                ppseudo_hdr->checksum = *pmsg++;
                for (i=1; i<7; i++) {
                    ppseudo_hdr->checksum ^= *pmsg++;
                }
                info->DSPInfoBlk[10] = 0x7200;
                info->DSPInfoBlk[11] = htons(info->DSPInfoBlklen);
                status = ft1000_write_dpram32 (dev, 0, (PUCHAR)&info->DSPInfoBlk[0], (unsigned short)(info->DSPInfoBlklen+22));
                status = ft1000_write_register (dev, FT1000_DB_DPRAM_TX, FT1000_REG_DOORBELL);
                info->DrvMsgPend = 0;

                break;
            }

          case GET_DRV_ERR_RPT_MSG: {
              DEBUG("FT1000:drivermsg:Got GET_DRV_ERR_RPT_MSG\n");
              // copy driver error message to dsp
              info->DrvMsgPend = 1;
              // allow any outstanding ioctl to finish
              mdelay(10);
              status = ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
              if (tempword & FT1000_DB_DPRAM_TX) {
                  mdelay(10);
                  status = ft1000_read_register(dev, &tempword, FT1000_REG_DOORBELL);
                  if (tempword & FT1000_DB_DPRAM_TX) {
                      mdelay(10);
                  }
              }

              if ( (tempword & FT1000_DB_DPRAM_TX) == 0) {
                  // Put message into Slow Queue
                  // Form Pseudo header
                  pmsg = (PUSHORT)&tempbuffer[0];
                  ppseudo_hdr = (PPSEUDO_HDR)pmsg;
                  ppseudo_hdr->length = htons(0x0012);
                  ppseudo_hdr->source = 0x10;
                  ppseudo_hdr->destination = 0x20;
                  ppseudo_hdr->portdest = 0;
                  ppseudo_hdr->portsrc = 0;
                  ppseudo_hdr->sh_str_id = 0;
                  ppseudo_hdr->control = 0;
                  ppseudo_hdr->rsvd1 = 0;
                  ppseudo_hdr->rsvd2 = 0;
                  ppseudo_hdr->qos_class = 0;
                  // Insert slow queue sequence number
                  ppseudo_hdr->seq_num = info->squeseqnum++;
                  // Insert application id
                  ppseudo_hdr->portsrc = 0;
                  // Calculate new checksum
                  ppseudo_hdr->checksum = *pmsg++;
                  for (i=1; i<7; i++) {
                      ppseudo_hdr->checksum ^= *pmsg++;
                  }
                  pmsg = (PUSHORT)&tempbuffer[16];
                  *pmsg++ = htons(RSP_DRV_ERR_RPT_MSG);
                  *pmsg++ = htons(0x000e);
                  *pmsg++ = htons(info->DSP_TIME[0]);
                  *pmsg++ = htons(info->DSP_TIME[1]);
                  *pmsg++ = htons(info->DSP_TIME[2]);
                  *pmsg++ = htons(info->DSP_TIME[3]);
                  convert.byte[0] = info->DspVer[0];
                  convert.byte[1] = info->DspVer[1];
                  *pmsg++ = convert.wrd;
                  convert.byte[0] = info->DspVer[2];
                  convert.byte[1] = info->DspVer[3];
                  *pmsg++ = convert.wrd;
                  *pmsg++ = htons(info->DrvErrNum);

                  CardSendCommand (dev, (unsigned char*)&tempbuffer[0], (USHORT)(0x0012 + PSEUDOSZ));
                  info->DrvErrNum = 0;
              }
              info->DrvMsgPend = 0;

          break;
      }

      default:
          break;
        }

    }

    status = STATUS_SUCCESS;
out:
    kfree(cmdbuffer);
    DEBUG("return from ft1000_proc_drvmsg\n");
    return status;
}



int ft1000_poll(void* dev_id) {

    //FT1000_INFO *info = (PFT1000_INFO)((struct net_device *)dev_id)->priv;
    //struct ft1000_device *dev = (struct ft1000_device *)info->pFt1000Dev;
    struct ft1000_device *dev = (struct ft1000_device *)dev_id;
	FT1000_INFO *info = (FT1000_INFO *) netdev_priv (dev->net);

    u16 tempword;
    u16 status;
    u16 size;
    int i;
    USHORT data;
    USHORT modulo;
    USHORT portid;
    u16 nxtph;
    PDPRAM_BLK pdpram_blk;
    PPSEUDO_HDR ppseudo_hdr;
    unsigned long flags;

    //DEBUG("Enter ft1000_poll...\n");
    if (ft1000_chkcard(dev) == FALSE) {
        DEBUG("ft1000_poll::ft1000_chkcard: failed\n");
        return STATUS_FAILURE;
    }

    status = ft1000_read_register (dev, &tempword, FT1000_REG_DOORBELL);
   // DEBUG("ft1000_poll: read FT1000_REG_DOORBELL message 0x%x\n", tempword);

    //while ( (tempword) && (!status) ) {
    if ( !status )
    {

        if (tempword & FT1000_DB_DPRAM_RX) {
            //DEBUG("ft1000_poll: FT1000_REG_DOORBELL message type:  FT1000_DB_DPRAM_RX\n");

            status = ft1000_read_dpram16(dev, 0x200, (PUCHAR)&data, 0);
            //DEBUG("ft1000_poll:FT1000_DB_DPRAM_RX:ft1000_read_dpram16:size = 0x%x\n", data);
            size = ntohs(data) + 16 + 2; //wai
            if (size % 4) {
                modulo = 4 - (size % 4);
                size = size + modulo;
            }
            status = ft1000_read_dpram16(dev, 0x201, (PUCHAR)&portid, 1);
            portid &= 0xff;
            //DEBUG("ft1000_poll: FT1000_REG_DOORBELL message type: FT1000_DB_DPRAM_RX : portid 0x%x\n", portid);

            if (size < MAX_CMD_SQSIZE) {
                switch (portid)
                {
                    case DRIVERID:
                        DEBUG("ft1000_poll: FT1000_REG_DOORBELL message type: FT1000_DB_DPRAM_RX : portid DRIVERID\n");

                        status = ft1000_proc_drvmsg (dev, size);
                        if (status != STATUS_SUCCESS )
                            return status;
                        break;
                    case DSPBCMSGID:
                        // This is a dsp broadcast message
                        // Check which application has registered for dsp broadcast messages
                        //DEBUG("ft1000_poll: FT1000_REG_DOORBELL message type: FT1000_DB_DPRAM_RX : portid DSPBCMSGID\n");

    	    	        for (i=0; i<MAX_NUM_APP; i++) {
        	           if ( (info->app_info[i].DspBCMsgFlag) && (info->app_info[i].fileobject) &&
                                         (info->app_info[i].NumOfMsg < MAX_MSG_LIMIT)  )
			   {
			       //DEBUG("Dsp broadcast message detected for app id %d\n", i);
			       nxtph = FT1000_DPRAM_RX_BASE + 2;
			       pdpram_blk = ft1000_get_buffer (&freercvpool);
			       if (pdpram_blk != NULL) {
			           if ( ft1000_receive_cmd(dev, pdpram_blk->pbuffer, MAX_CMD_SQSIZE, &nxtph) ) {
				       ppseudo_hdr = (PPSEUDO_HDR)pdpram_blk->pbuffer;
				       // Put message into the appropriate application block
				       info->app_info[i].nRxMsg++;
				       spin_lock_irqsave(&free_buff_lock, flags);
				       list_add_tail(&pdpram_blk->list, &info->app_info[i].app_sqlist);
				       info->app_info[i].NumOfMsg++;
				       spin_unlock_irqrestore(&free_buff_lock, flags);
				       wake_up_interruptible(&info->app_info[i].wait_dpram_msg);
                                   }
                                   else {
				       info->app_info[i].nRxMsgMiss++;
				       // Put memory back to free pool
				       ft1000_free_buffer(pdpram_blk, &freercvpool);
				       DEBUG("pdpram_blk::ft1000_get_buffer NULL\n");
                                   }
                               }
                               else {
                                   DEBUG("Out of memory in free receive command pool\n");
                                   info->app_info[i].nRxMsgMiss++;
                               }//endof if (pdpram_blk != NULL)
                           }//endof if
    		           //else
    		           //    DEBUG("app_info mismatch\n");
	                }// endof for
                        break;
                    default:
                        pdpram_blk = ft1000_get_buffer (&freercvpool);
                        //DEBUG("Memory allocated = 0x%8x\n", (u32)pdpram_blk);
                        if (pdpram_blk != NULL) {
                           if ( ft1000_receive_cmd(dev, pdpram_blk->pbuffer, MAX_CMD_SQSIZE, &nxtph) ) {
                               ppseudo_hdr = (PPSEUDO_HDR)pdpram_blk->pbuffer;
                               // Search for correct application block
                               for (i=0; i<MAX_NUM_APP; i++) {
                                   if (info->app_info[i].app_id == ppseudo_hdr->portdest) {
                                       break;
                                   }
                               }

                               if (i==(MAX_NUM_APP-1)) {		// aelias [+] reason: was out of array boundary
                                   info->app_info[i].nRxMsgMiss++;
                                   DEBUG("FT1000:ft1000_parse_dpram_msg: No application matching id = %d\n", ppseudo_hdr->portdest);
                                   // Put memory back to free pool
                                   ft1000_free_buffer(pdpram_blk, &freercvpool);
                               }
                               else {
                                   if (info->app_info[i].NumOfMsg > MAX_MSG_LIMIT) {
	                               // Put memory back to free pool
	                               ft1000_free_buffer(pdpram_blk, &freercvpool);
                                   }
                                   else {
                                       info->app_info[i].nRxMsg++;
                                       // Put message into the appropriate application block
                                       //pxu spin_lock_irqsave(&free_buff_lock, flags);
                                       list_add_tail(&pdpram_blk->list, &info->app_info[i].app_sqlist);
            			       info->app_info[i].NumOfMsg++;
                                       //pxu spin_unlock_irqrestore(&free_buff_lock, flags);
                                       //pxu wake_up_interruptible(&info->app_info[i].wait_dpram_msg);
                                   }
                               }
                           }
                           else {
                               // Put memory back to free pool
                               ft1000_free_buffer(pdpram_blk, &freercvpool);
                           }
                        }
                        else {
                            DEBUG("Out of memory in free receive command pool\n");
                        }
                        break;
                } //end of switch
            } //endof if (size < MAX_CMD_SQSIZE)
            else {
                DEBUG("FT1000:dpc:Invalid total length for SlowQ = %d\n", size);
            }
            status = ft1000_write_register (dev, FT1000_DB_DPRAM_RX, FT1000_REG_DOORBELL);
        }
        else if (tempword & FT1000_DSP_ASIC_RESET) {
            //DEBUG("ft1000_poll: FT1000_REG_DOORBELL message type:  FT1000_DSP_ASIC_RESET\n");

            // Let's reset the ASIC from the Host side as well
            status = ft1000_write_register (dev, ASIC_RESET_BIT, FT1000_REG_RESET);
            status = ft1000_read_register (dev, &tempword, FT1000_REG_RESET);
            i = 0;
            while (tempword & ASIC_RESET_BIT) {
                status = ft1000_read_register (dev, &tempword, FT1000_REG_RESET);
                msleep(10);
                i++;
                if (i==100)
                    break;
            }
            if (i==100) {
                DEBUG("Unable to reset ASIC\n");
                return STATUS_SUCCESS;
            }
            msleep(10);
            // Program WMARK register
            status = ft1000_write_register (dev, 0x600, FT1000_REG_MAG_WATERMARK);
            // clear ASIC reset doorbell
            status = ft1000_write_register (dev, FT1000_DSP_ASIC_RESET, FT1000_REG_DOORBELL);
            msleep(10);
        }
        else if (tempword & FT1000_ASIC_RESET_REQ) {
            DEBUG("ft1000_poll: FT1000_REG_DOORBELL message type:  FT1000_ASIC_RESET_REQ\n");

            // clear ASIC reset request from DSP
            status = ft1000_write_register (dev, FT1000_ASIC_RESET_REQ, FT1000_REG_DOORBELL);
            status = ft1000_write_register (dev, HOST_INTF_BE, FT1000_REG_SUP_CTRL);
            // copy dsp session record from Adapter block
            status = ft1000_write_dpram32 (dev, 0, (PUCHAR)&info->DSPSess.Rec[0], 1024);
            // Program WMARK register
            status = ft1000_write_register (dev, 0x600, FT1000_REG_MAG_WATERMARK);
            // ring doorbell to tell DSP that ASIC is out of reset
            status = ft1000_write_register (dev, FT1000_ASIC_RESET_DSP, FT1000_REG_DOORBELL);
        }
        else if (tempword & FT1000_DB_COND_RESET) {
            DEBUG("ft1000_poll: FT1000_REG_DOORBELL message type:  FT1000_DB_COND_RESET\n");
//By Jim
// Reset ASIC and DSP
//MAG
            if (info->fAppMsgPend == 0) {
               // Reset ASIC and DSP

                status    = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER0, (PUCHAR)&(info->DSP_TIME[0]), FT1000_MAG_DSP_TIMER0_INDX);
                status    = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER1, (PUCHAR)&(info->DSP_TIME[1]), FT1000_MAG_DSP_TIMER1_INDX);
                status    = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER2, (PUCHAR)&(info->DSP_TIME[2]), FT1000_MAG_DSP_TIMER2_INDX);
                status    = ft1000_read_dpram16(dev, FT1000_MAG_DSP_TIMER3, (PUCHAR)&(info->DSP_TIME[3]), FT1000_MAG_DSP_TIMER3_INDX);
                info->CardReady = 0;
                info->DrvErrNum = DSP_CONDRESET_INFO;
                DEBUG("ft1000_hw:DSP conditional reset requested\n");
                info->ft1000_reset(dev->net);
            }
            else {
                info->fProvComplete = 0;
                info->fCondResetPend = 1;
            }

            ft1000_write_register(dev, FT1000_DB_COND_RESET, FT1000_REG_DOORBELL);
        }

    }//endof if ( !status )

    //DEBUG("return from ft1000_poll.\n");
    return STATUS_SUCCESS;

}

/*end of Jim*/
