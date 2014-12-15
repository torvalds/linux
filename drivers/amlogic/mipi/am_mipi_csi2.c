/*******************************************************************
 *
 *  Copyright C 2012 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2012/3/13   19:46
 *
 *******************************************************************/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <asm/delay.h>
#include <asm/atomic.h>
#include <linux/pm_runtime.h>
#include <mach/am_regs.h>
#include <linux/videodev2.h>
#include <mach/mipi_phy_reg.h>
#include <linux/mipi/am_mipi_csi2.h>
#include <media/amlogic/aml_camera.h>
#include <linux/module.h>
#include "common/bufq.h"
#ifdef CONFIG_USE_OF
#include <linux/of.h> //device tree header
#endif

#define DEVICE_NAME "am-mipi-csi2"

#ifdef CONFIG_MEM_MIPI
extern const struct am_csi2_ops_s am_csi2_mem;
#endif

#ifdef CONFIG_VDIN_MIPI
extern const struct am_csi2_ops_s am_csi2_vdin;
#endif

static const struct am_csi2_ops_s* csi2_ops[]=
{
#ifdef CONFIG_MEM_MIPI
    &am_csi2_mem,
#endif
#ifdef CONFIG_VDIN_MIPI
    &am_csi2_vdin,
#endif
};

static am_csi2_t am_csi2_para[] =
{
    {
        //inited when probe
        .id = -1,
        .pdev = NULL,
#ifdef CONFIG_MEM_MIPI
        .irq = -1,
#endif
        .pbufAddr = 0x81000000,
        .decbuf_size = 0x70000,

        //inited when start csi2
        .client = NULL,
        .ui_val = 0,
        .hs_freq = 0,
        .clock_lane_mode = 0,
        .mirror = 0,
        .frame_rate = 0,
        .status = AM_CSI2_FLAG_NULL,

        .output = {0},
        .input = {0},
        .ops = NULL,
    },
};

static void release_frame(am_csi2_t *dev)
{
    int i = 0;
    for( i= 0;i<CSI2_BUF_POOL_SIZE;i++){
        dev->input.frame[i].ddr_address = 0;
        dev->input.frame[i].index = -1;
        dev->input.frame[i].status = AM_CSI2_BUFF_STATUS_NULL;
        dev->input.frame[i].w = 0;
        dev->input.frame[i].h = 0;
        dev->input.frame[i].read_cnt = 0;
        dev->input.frame[i].err = 0;
    }
    for( i= 0;i<CSI2_OUTPUT_BUF_POOL_SIZE;i++){
        dev->output.frame[i].ddr_address = 0;
        dev->output.frame[i].index = -1;
        dev->output.frame[i].status = AM_CSI2_BUFF_STATUS_NULL;
        dev->output.frame[i].w = 0;
        dev->output.frame[i].h = 0;
        dev->output.frame[i].read_cnt = 0;
        dev->output.frame[i].err = 0;
    }
}

static am_csi2_t* find_csi2_dev(const char* client_name)
{
    am_csi2_t *r = NULL;
    am_csi2_t *dev = NULL;
    struct am_csi2_pdata *pdata = NULL;
#ifndef CONFIG_USE_OF
    aml_plat_cam_data_t* pdev = NULL;
#else
    struct am_csi2_client_config *clt_cfg=NULL;
#endif

    int i =0,j = 0;

    for(i = 0;i<ARRAY_SIZE(am_csi2_para);i++){
        dev = &am_csi2_para[i];
        pdata = dev->pdev->dev.platform_data;
        for(j =0;j<pdata->num_clients;j++){
#ifndef CONFIG_USE_OF
            pdev = (aml_plat_cam_data_t *)(pdata->clients[j].pdev);
            if(strcmp(client_name,pdev->name)==0){
                dev->client = &pdata->clients[j];
                r = dev;
                break;
            }
#else
            clt_cfg = (struct am_csi2_client_config *) &(pdata->clients[j]);
            if(strcmp(client_name, clt_cfg->name)==0){
                dev->client = &pdata->clients[j];
                r = dev;
                break;
            }
#endif
        }
    }
    return r;
}

static void stop_mipi_dev(am_csi2_t *dev)
{
    dev->ops->streamoff(dev);
    dev->ops->uninit(dev);
    release_frame(dev);
    memset(&dev->input, 0,sizeof(am_csi2_input_t));
    memset(&dev->output, 0,sizeof(am_csi2_output_t));
    dev->ops = NULL;
    dev->client = NULL;
    dev->status &= (~AM_CSI2_FLAG_STARTED);  
    dev->status &= (~AM_CSI2_FLAG_DEV_READY); 
    return;
}

int start_mipi_csi2_service(struct am_csi2_camera_para *para)
{
    am_csi2_t *dev = NULL;
    struct am_csi2_ops_s* ops = NULL;
    unsigned in_frame_size = 0,i = 0,out_frame_size = 0;
    unsigned decbuf_start = 0;
    int ret = -1;
    struct am_csi2_pixel_fmt* in_fmt = NULL;
    struct am_csi2_pixel_fmt* out_fmt = NULL;

    if(!para){
        mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with parameter error. \n");
        return -1;
    }

    dev = find_csi2_dev(para->name);
    if(!dev){
        mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with no device. \n");
        return -1;
    }

    if(!dev->client){
        mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with no client. \n");
        return -1;
    }

    mutex_lock(&dev->lock);
    if(!(dev->status&AM_CSI2_FLAG_INITED)){
        mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with no init. \n");
        goto exit;
    }

    if(dev->status&AM_CSI2_FLAG_STARTED){
        mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with already statrt. \n");
        goto exit;
    }

    for(i = 0;i<ARRAY_SIZE(csi2_ops);i++){
        ops = (struct am_csi2_ops_s*)csi2_ops[i];
        if(ops->mode == dev->client->mode){
            dev->ops = ops;
            break;
        }
    }

    if(!dev->ops){
        mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with no ops. \n");
        goto err_exit;
    }

    dev->frame_rate = para->frame_rate;
    dev->ui_val = para->ui_val;
    dev->hs_freq = para->hs_freq;
    dev->clock_lane_mode = para->clock_lane_mode;
    dev->mirror = para->mirror;

    in_fmt = dev->ops->getPixelFormat(para->in_fmt->fourcc,true);
    out_fmt= dev->ops->getPixelFormat(para->out_fmt->fourcc,false);

    if((!in_fmt)||(!out_fmt)){
        mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with unsupported pixel format. \n");
        goto err_exit;
    }

    release_frame(dev);

    dev->input.active_pixel = (para->active_pixel +0x1) & ~0x1;
    dev->input.active_line = (para->active_line + 0x7) & ~0x7;

    dev->output.output_line= (para->output_line+ 0x1) & ~0x1;
    dev->output.output_pixel= (para->output_pixel+ 0x1) & ~0x1;

    dev->input.depth = in_fmt->depth;
    dev->input.fourcc= in_fmt->fourcc;
    dev->output.depth = out_fmt->depth;
    dev->output.fourcc = out_fmt->fourcc;

    out_frame_size = ((dev->output.output_pixel*dev->output.output_line *dev->output.depth)>>3);
    in_frame_size = ((dev->input.active_pixel*dev->input.active_line*dev->input.depth)>>3)+0x200;

    if(dev->ops->mode == AM_CSI2_ALL_MEM){
        if(dev->decbuf_size<(out_frame_size+in_frame_size*3)){
            mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with too small buff size for mem mode. \n");
            goto err_exit;
        }
        //configure input buff for mipi receiver
        if(dev->decbuf_size>=(out_frame_size+in_frame_size*CSI2_BUF_POOL_SIZE)){
            dev->input.frame_available = CSI2_BUF_POOL_SIZE;
        }else{
            dev->input.frame_available = (dev->decbuf_size-out_frame_size)/in_frame_size;
        }
        dev->input.frame_size = in_frame_size;
        for(i = 0;i<dev->input.frame_available ;i++){
            dev->input.frame[i].ddr_address = dev->pbufAddr+(i*in_frame_size);
            dev->input.frame[i].status= AM_CSI2_BUFF_STATUS_FREE;
        }

        //configure output buff for convert
        decbuf_start = dev->pbufAddr+in_frame_size*dev->input.frame_available;
        dev->output.frame_available = (dev->decbuf_size-in_frame_size*dev->input.frame_available )/out_frame_size;
        //if(dev->output.frame_available>CSI2_BUF_POOL_SIZE)
        //    dev->output.frame_available = CSI2_BUF_POOL_SIZE;
        if(dev->output.frame_available>CSI2_OUTPUT_BUF_POOL_SIZE)
            dev->output.frame_available = CSI2_OUTPUT_BUF_POOL_SIZE;
            dev->output.frame_size = out_frame_size;
        for(i = 0;i<dev->output.frame_available ;i++){
            dev->output.frame[i].ddr_address = decbuf_start+(i*out_frame_size);
            dev->output.frame[i].status= AM_CSI2_BUFF_STATUS_FREE;
        }
    }else{
        // for vdin mode, input buff will be provided from vdin memory, mipi doesn't care input buff. only need config the output buff
        if(dev->decbuf_size<out_frame_size){
            mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with too small buff size for vdin mode. \n");
            goto err_exit;
        }

        //need not configure input buff now
        dev->input.frame_available = 0;//CSI2_BUF_POOL_SIZE;

        dev->input.frame_size = in_frame_size;

        //configure output buff for convert
        decbuf_start = dev->pbufAddr;
        dev->output.frame_available = dev->decbuf_size/out_frame_size;
        if(dev->output.frame_available>CSI2_OUTPUT_BUF_POOL_SIZE)
            dev->output.frame_available = CSI2_OUTPUT_BUF_POOL_SIZE;
            dev->output.frame_size = out_frame_size;
        for(i = 0;i<dev->output.frame_available ;i++){
            dev->output.frame[i].ddr_address = decbuf_start+(i*out_frame_size);
            dev->output.frame[i].status= AM_CSI2_BUFF_STATUS_FREE;
        }
    }

    dev->output.vaddr = NULL;

    mipi_dbg("[am_mipi_csi2]:start_mipi_csi2_service--config input buff 0x%xx%d, output buff: 0x%xx%d.\n",
        in_frame_size,dev->input.frame_available,
        out_frame_size,dev->output.frame_available);

    if(!(dev->status & AM_CSI2_FLAG_DEV_READY)){
        if(dev->ops->init(dev)<0){
            mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with ops init fail. \n");
            goto err_exit;
        }
    }
    dev->status |= AM_CSI2_FLAG_DEV_READY;
    if(dev->ops->streamon(dev)<0){
        mipi_error("[am_mipi_csi2]:start mipi csi2 service fail with ops stream on fail. \n");
        goto err_exit;   
    }

    msleep(10);  
    dev->status |= AM_CSI2_FLAG_STARTED;
    ret = 0;
err_exit:
    if(ret<0)
        stop_mipi_dev(dev);
exit:
    mutex_unlock(&dev->lock);
    return ret;
}

int stop_mipi_csi2_service(struct am_csi2_camera_para *para)
{
    am_csi2_t *dev = NULL;

    dev = find_csi2_dev(para->name);
    if(!dev){
        mipi_error("[am_mipi_csi2]:stop mipi csi2 service fail with no device. \n");
        return -1;
    }

    mutex_lock(&dev->lock);

    dev->ops->streamoff(dev);
    release_frame(dev);
    memset(&dev->input, 0,sizeof(am_csi2_input_t));
    memset(&dev->output, 0,sizeof(am_csi2_output_t));
    dev->ops = NULL;
    dev->client = NULL;
    dev->status &= (~AM_CSI2_FLAG_STARTED); 
    mutex_unlock(&dev->lock);
    return 0;
}

int am_csi2_fill_buffer(struct am_csi2_camera_para *para,void* output)
{
    am_csi2_t *dev = NULL;
    int ret = -1;
    if((!para)||(!output)){
        mipi_error("[am_mipi_csi2]:am csi2 fill buffer error, error pointer. \n");
        return -1;
    }
    dev = find_csi2_dev(para->name);
    if(!dev){
        mipi_error("[am_mipi_csi2]:am csi2 fill buffer error, no device. \n");
        return -1;
    }
    if((!(dev->status&AM_CSI2_FLAG_INITED))
      ||(!(dev->status&AM_CSI2_FLAG_DEV_READY))
      ||(!(dev->status&AM_CSI2_FLAG_STARTED))){
        mipi_error("[am_mipi_csi2]:am csi2 fill buffer error, device not ready. \n");
        return -1;
    }

    mutex_lock(&dev->lock);
    dev->output.vaddr = output;
    dev->output.zoom= para->zoom;
#ifdef CONFIG_AMLOGIC_CAPTURE_FRAME_ROTATE
    dev->output.angle= para->angle;
#else
    dev->output.angle= 0;
#endif
    //dev->output.output_line= para->output_line;
    //dev->output.output_pixel= para->output_pixel;
    //dev->output.fourcc= para->out_fmt->fourcc;
    //dev->output.depth= para->out_fmt->depth;
    ret = dev->ops->fill(dev);
    mutex_unlock(&dev->lock);
    return 0;
}

#ifdef CONFIG_USE_OF
static struct am_csi2_client_config am_mipi_csi2_clients[] = {
    {
        .mode = AM_CSI2_VDIN,//AM_CSI2_ALL_MEM,//
        .lanes = 1,
        .channel = 0x1,
        .vdin_num = 0,

        .name = "mipi-hi2056", //**need check
        .pdev = NULL,//(void *)&mipi_hi2056_data,
    },
};

static struct am_csi2_pdata am_mipi_csi2_info = {
    .clients = am_mipi_csi2_clients,
    .num_clients = ARRAY_SIZE(am_mipi_csi2_clients),
};

#define AMLOGIC_CSI_DRV_DATA ((kernel_ulong_t)&am_mipi_csi2_info)
static const struct of_device_id csi_dt_match[] = {
	{
		.compatible = "amlogic,csi",
		.data = (void *)AMLOGIC_CSI_DRV_DATA,
	},
	{},
};

#else
#define csi_dt_match NULL
#endif


static int am_mipi_csi2_probe(struct platform_device *pdev)
{
    struct resource *res = NULL;
#ifdef CONFIG_MEM_MIPI
    int irq = -1;
#endif
    am_csi2_t*dev = NULL;
    struct am_csi2_pdata *pdata = NULL;
	/* Platform data specify the PHY, lanes */
#ifdef CONFIG_USE_OF
    const struct of_device_id *match;

    if (pdev->dev.of_node){
            printk("of_node=%p\n", pdev->dev.of_node);
            match = of_match_node(csi_dt_match, pdev->dev.of_node);
            pdata =  (struct aml_csi2_pdata *)match->data;
    }
#else
    pdata = pdev->dev.platform_data;
#endif

    mipi_dbg("[am_mipi_csi2]:am_mipi_csi2 probe start.\n");

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
#ifdef CONFIG_MEM_MIPI
    irq = platform_get_irq(pdev, 0);
    if (irq <= 0 || !pdata) {
        mipi_error("[am_mipi_csi2]:am csi2 irq resource undefined.\n");
        return -ENODEV;
    }
#endif
    if (!res || !pdata) {
        mipi_error("[am_mipi_csi2]:am csi2 memory resource undefined.\n");
        return -ENODEV;
    }

    if (pdev->id>=ARRAY_SIZE(am_csi2_para)) {
        mipi_error("[am_mipi_csi2]:am csi2 device id error.\n");
        return -EINVAL;
    }

    printk("pdev->id=%d\n", pdev->id);
    dev = (am_csi2_t*)&am_csi2_para[pdev->id];

    memset(dev,0,sizeof(am_csi2_t));

    mutex_init(&dev->lock);
    dev->id = pdev->id;
    dev->pbufAddr = res->start;
    dev->decbuf_size= res->end - res->start + 1;

#ifdef CONFIG_MEM_MIPI
    dev->irq = irq;
#endif
    dev->pdev = pdev;
    dev->status |= AM_CSI2_FLAG_INITED;
    platform_set_drvdata(pdev, dev);
    pm_runtime_enable(&pdev->dev);
    mipi_dbg("[am_mipi_csi2]:am_mipi_csi2 probe end.\n");
    return 0;
}

static int am_mipi_csi2_remove(struct platform_device *pdev)
{
    /* Remove the cdev */
    am_csi2_t *dev = platform_get_drvdata(pdev);

    mutex_lock(&dev->lock);

    stop_mipi_dev(dev);
    dev->status &= (~AM_CSI2_FLAG_INITED);

    mutex_unlock(&dev->lock);
    mutex_destroy(&dev->lock);
    pm_runtime_disable(&pdev->dev);
    platform_set_drvdata(pdev, NULL);
    return 0;
}

#if 0//def CONFIG_PM_SLEEP
static int am_mipi_csi2_suspend(struct device *dev)
{
    return 0;
}

static int am_mipi_csi2_resume(struct device *dev)
{
    return 0;
}

static int am_mipi_csi2_pm_suspend(struct device *dev)
{
    return am_mipi_csi2_suspend(dev);
}

static int am_mipi_csi2_pm_resume(struct device *dev)
{
    int ret = am_mipi_csi2_resume(dev);

    if (!ret) {
        pm_runtime_disable(dev);
        ret = pm_runtime_set_active(dev);
        pm_runtime_enable(dev);
    }
    return ret;
}

static const struct dev_pm_ops am_mipi_csi2_pm_ops = {
    SET_RUNTIME_PM_OPS(am_mipi_csi2_suspend, am_mipi_csi2_resume, NULL)
    SET_SYSTEM_SLEEP_PM_OPS(am_mipi_csi2_pm_suspend, am_mipi_csi2_pm_resume)
};
#endif

static struct platform_driver am_mipi_csi2_driver = {
    .probe		= am_mipi_csi2_probe,
    .remove	= am_mipi_csi2_remove,
    .driver		= {
        .name	= DEVICE_NAME,
        .owner	= THIS_MODULE,
#ifdef CONFIG_USE_OF
        .of_match_table = csi_dt_match,
#endif
#if 0//def CONFIG_PM_SLEEP
        .pm		= &am_mipi_csi2_pm_ops,
#endif
    }
};

static int __init am_mipi_csi2_init_module(void)
{
    mipi_dbg("[am_mipi_csi2]:am_mipi_csi2 module init\n");
    if (platform_driver_register(&am_mipi_csi2_driver)) {
        mipi_error("[am_mipi_csi2]:failed to register am_mipi_csi2 driver\n");
        return -ENODEV;
    }
    return 0;
}

static void __exit am_mipi_csi2_exit_module(void)
{
    mipi_dbg("[am_mipi_csi2]:am_mipi_csi2 module remove.\n");
    platform_driver_unregister(&am_mipi_csi2_driver);
    return ;
}

module_init(am_mipi_csi2_init_module);
module_exit(am_mipi_csi2_exit_module);

MODULE_DESCRIPTION("AMLOGIC MIPI CSI2 driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
