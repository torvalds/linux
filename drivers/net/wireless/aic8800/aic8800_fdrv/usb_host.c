/**
 * usb_host.c
 *
 * USB host function declarations
 *
 * Copyright (C) AICSemi 2018-2020
 */


#include "usb_host.h"
//#include "ipc_compat.h"
#include "rwnx_tx.h"
#include "rwnx_platform.h"
#include "aicwf_debug.h"

/**
 ****************************************************************************************
 */
void aicwf_usb_host_init(struct usb_host_env_tag *env,
                  void *cb,
                  void *shared_env_ptr,
                  void *pthis)
{
    // Reset the environments

    // Reset the Host environment
    memset(env, 0, sizeof(struct usb_host_env_tag));
    // Save the pointer to the register base
    env->pthis = pthis;
}

/**
 ****************************************************************************************
 */
volatile struct txdesc_host *aicwf_usb_host_txdesc_get(struct usb_host_env_tag *env, const int queue_idx)
{
 //   struct ipc_shared_env_tag *shared_env_ptr = env->shared;
    volatile struct txdesc_host *txdesc_free = NULL;
    uint32_t used_idx = env->txdesc_used_idx[queue_idx];
    uint32_t free_idx = env->txdesc_free_idx[queue_idx];

   // ASSERT_ERR(queue_idx < SDIO_TXQUEUE_CNT);
   // ASSERT_ERR((free_idx - used_idx) <= USB_TXDESC_CNT);

    // Check if a free descriptor is available
    if (free_idx != (used_idx + USB_TXDESC_CNT))
    {
        // Get the pointer to the first free descriptor
    //    txdesc_free = shared_env_ptr->txdesc[queue_idx] + (free_idx % IPC_TXDESC_CNT);
    }
    else
    {
        //txdesc_free = NULL;
    }

    return txdesc_free;
}

/**
 ****************************************************************************************
 */
void aicwf_usb_host_txdesc_push(struct usb_host_env_tag *env, const int queue_idx, const uint64_t host_id)
{
    //printk("push, %d, %d, 0x%llx \r\n", queue_idx, env->txdesc_free_idx[queue_idx], host_id);
    // Save the host id in the environment
    env->tx_host_id[queue_idx][env->txdesc_free_idx[queue_idx] % USB_TXDESC_CNT] = host_id;

    // Increment the index
    env->txdesc_free_idx[queue_idx]++;
    if(env->txdesc_free_idx[queue_idx]==0x40000000)
        env->txdesc_free_idx[queue_idx] = 0;
}

/**
 ****************************************************************************************
 */
void aicwf_usb_host_tx_cfm_handler(struct usb_host_env_tag *env, u32 *data)
{
    u32 queue_idx  = 0;//data[0];
    //struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)env->pthis;
    struct sk_buff *skb = NULL;
    struct rwnx_txhdr *txhdr;
	AICWFDBG(LOGTRACE, "%s Enter \n", __func__);
    //printk("sdio_host_tx_cfm_handler, %d, 0x%08x\r\n", queue_idx, data[1]);
    // TX confirmation descriptors have been received
   // REG_SW_SET_PROFILING(env->pthis, SW_PROF_IRQ_E2A_TXCFM);
    //while (1)
    {
        // Get the used index and increase it. We do the increase before knowing if the
        // current buffer is confirmed because the callback function may call the
        // ipc_host_txdesc_get() in case flow control was enabled and the index has to be
        // already at the good value to ensure that the test of FIFO full is correct
        //uint32_t used_idx = env->txdesc_used_idx[queue_idx]++;
		uint32_t used_idx = data[1];
        //uint64_t host_id = env->tx_host_id[queue_idx][used_idx % USB_TXDESC_CNT];
        unsigned long host_id = env->tx_host_id[queue_idx][used_idx % USB_TXDESC_CNT];

        // Reset the host id in the array
        env->tx_host_id[queue_idx][used_idx % USB_TXDESC_CNT] = 0;

        // call the external function to indicate that a TX packet is freed
        if (host_id == 0)
        {
            // No more confirmations, so put back the used index at its initial value
            env->txdesc_used_idx[queue_idx] = used_idx;
			AICWFDBG(LOGERROR, "ERROR:No more confirmations\r\n");
            return;
            //break;
        }
		AICWFDBG(LOGINFO, "usb_host_tx_cfm_handler:used_idx=%d, hostid=%p, 0x%p, status=%x\r\n",used_idx, (void *)host_id, env->pthis, data[0]);

        // set the cfm status
        skb = (struct sk_buff *)host_id;
        txhdr = (struct rwnx_txhdr *)skb->data;
        txhdr->hw_hdr.cfm.status = (union rwnx_hw_txstatus)data[0];
        //txhdr->hw_hdr.status = data[1];
        //if (env->cb.send_data_cfm(env->pthis, host_id) != 0)
        if (rwnx_txdatacfm(env->pthis, (void *)host_id) != 0)
        {
            // No more confirmations, so put back the used index at its initial value
            env->txdesc_used_idx[queue_idx] = used_idx;
            env->tx_host_id[queue_idx][used_idx % USB_TXDESC_CNT] = host_id;
            // and exit the loop
            AICWFDBG(LOGERROR, "ERROR:rwnx_txdatacfm,\r\n");
          //  break;
        }

    }
}

int aicwf_rwnx_usb_platform_init(struct aic_usb_dev *usbdev)
{
    struct rwnx_plat *rwnx_plat = NULL;
    void *drvdata;
    int ret = -ENODEV;

    rwnx_plat = kzalloc(sizeof(struct rwnx_plat), GFP_KERNEL);

    if (!rwnx_plat)
        return -ENOMEM;

//    rwnx_plat->pci_dev = pci_dev;
    rwnx_plat->usbdev = usbdev;

    ret = rwnx_platform_init(rwnx_plat, &drvdata);
#if 0
    pci_set_drvdata(pci_dev, drvdata);

    if (ret)
        rwnx_plat->deinit(rwnx_plat);
#endif
    return ret;
}

