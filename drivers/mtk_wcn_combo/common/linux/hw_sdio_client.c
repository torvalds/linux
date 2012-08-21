
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>

#include "hw_test.h"



extern unsigned char** g_read_card_info_bysdio;
extern UINT8 g_read_current_io_isready_from_sdio_card;
extern UINT16 g_read_function_blksize_from_sdio_card;
extern UINT32 g_read_int_from_sdio_card;

static const struct sdio_device_id hw_sdio_id_tbl[] = {

    /* MT6620 */ /* Not an SDIO standard class device */
    { SDIO_DEVICE(0x037A, 0x020A) }, /* SDIO1:FUNC1:WIFI */
    { SDIO_DEVICE(0x037A, 0x020B) }, /* SDIO2:FUNC1:BT+FM+GPS */
    { SDIO_DEVICE(0x037A, 0x020C) }, /* 2-function (SDIO2:FUNC1:BT+FM+GPS, FUNC2:WIFI) */
    	{},
};

MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(sdio, hw_sdio_id_tbl);

static int hw_sdio_probe (
    struct sdio_func *func,
    const struct sdio_device_id *id
    );

static void hw_sdio_remove (
    struct sdio_func *func
    );
INT32 read_card_info(struct sdio_func * func,UINT32 offset,PUINT32 pbuf,UINT32 len);

static struct sdio_driver hw_sdio_client_drv = {
    .name = "hw_sdio_client", /* MTK SDIO Client Driver */
    .id_table = hw_sdio_id_tbl, /* all supported struct sdio_device_id table */
    .probe = hw_sdio_probe,
    .remove = hw_sdio_remove,
};

UINT8 read_sdio_card_register_by_cmd52(struct sdio_func * func)
{
	UINT8  val;
	INT32 ret = -1;
	
	sdio_claim_host(func);
	val = sdio_f0_readb(func,0x03,&ret);
	sdio_release_host(func);
	if(val == 0xff){
		HWTEST_INFO_FUNC("read CCCR I/O ready register by cmd52 fail!\n");
		return -1;
	}else{
		HWTEST_INFO_FUNC("read CCCR I/O ready register by cmd52:0x%08x\n",val);
		return val;
	}
}

UINT32 read_sdio_card_register_by_cmd53(struct sdio_func * func)
{
	UINT32 val;
	INT32 ret = -1;
	sdio_claim_host(func);
	val = sdio_readl(func,0x0000,&ret);
	sdio_release_host(func);
	if(val == 0xffffffff){
		HWTEST_INFO_FUNC("read sdio register by cmd53 fail!\n");
		return -1;
	}else{
		HWTEST_INFO_FUNC("read sdio register by cmd53:0x%08x\n",val);
		return val;
	}
}

UINT16 read_sdio_card_cccr_register(struct sdio_func * func)
{
	UINT8 val1, val2;
	INT32 ret = -1;
	UINT16 result = 0;
	
	sdio_claim_host(func);
	val1 = sdio_f0_readb(func,0x110,&ret);
	sdio_release_host(func);
	if(val1 == 0xff){
		HWTEST_INFO_FUNC("read sdio cccr register fail!\n");
		return -1;
	}else{
		HWTEST_INFO_FUNC("read current function blksize_first byte:0x%08x\n",val1);
	}
	
	sdio_claim_host(func);
	val2 = sdio_f0_readb(func,0x111,&ret);
	sdio_release_host(func);
	if(val2 == 0xff){
		HWTEST_INFO_FUNC("read sdio cccr register fail!\n");
		return -1;
	}else{
		HWTEST_INFO_FUNC("read current function blksize_second byte:0x%08x\n",val2);
	}
	result = val2 << 8;
	result |= val1;
	return result;
}

static int hw_sdio_probe (struct sdio_func *func,const struct sdio_device_id *id)
{
	int i = 0;
	char buf[128];
	
	HWTEST_INFO_FUNC("%s start \n",__FUNCTION__);
    if(!func){
		HWTEST_INFO_FUNC("func pointer is null!\n");
		return -1;
	}

    //4 <0> display debug information
    HWTEST_INFO_FUNC("vendor(0x%x) device(0x%x) num(0x%x)\n", func->vendor, func->device, func->num);
    for (i = 0;i < func->card->num_info;i++) {
        HWTEST_INFO_FUNC("card->info[%d]: %s\n", i, func->card->info[i]);
    }
	g_read_card_info_bysdio = (char **)kmalloc((func->card->num_info) * sizeof(char*),GFP_KERNEL);


	
	for(i = 0; i < func->card->num_info;i++){

		HWTEST_INFO_FUNC("card info str(%d) strlen = %d\n",i,strlen(func->card->info[i]));
		
		if(!g_read_card_info_bysdio[i]){
			HWTEST_INFO_FUNC("kmalloc buffer fail!\n");
			return -2;
		}
		if(0 != strlen(func->card->info[i])){
			g_read_card_info_bysdio[i] = kmalloc(strlen(func->card->info[i]) * sizeof(char),GFP_KERNEL);
			strcpy(g_read_card_info_bysdio[i],func->card->info[i]);
			HWTEST_INFO_FUNC("g_read_card_info_bysdio[%d] = %s\n",i,g_read_card_info_bysdio[i]);
		}
		else{
			g_read_card_info_bysdio[i] = kmalloc(128,GFP_KERNEL);
			memset(g_read_card_info_bysdio[i],0,128);
		}
	}
	

	sprintf(buf,"verdor is 0x%x,device is 0x%x,func number is 0x%x",func->vendor,func->device,func->num);
	HWTEST_INFO_FUNC("buf = %s\n",buf);
	for(i = 0; i < func->card->num_info;i++){
		if(!g_read_card_info_bysdio[i][0]){
			strcpy(g_read_card_info_bysdio[i],buf);
			break;
		}
	}
	

	sdio_claim_host(func);
    sdio_enable_func(func);
    sdio_release_host(func);

	sdio_claim_host(func);
	sdio_set_block_size(func, 512);
	sdio_release_host(func);
		
	g_read_current_io_isready_from_sdio_card = read_sdio_card_register_by_cmd52(func);
	g_read_function_blksize_from_sdio_card = read_sdio_card_cccr_register(func);
	g_read_int_from_sdio_card = read_sdio_card_register_by_cmd53(func);
	
	HWTEST_INFO_FUNC("%s end\n", __FUNCTION__);
	return 0;
}

static void hw_sdio_remove (struct sdio_func *func)
{
	int i = 0;
	HWTEST_INFO_FUNC("%s start \n",__FUNCTION__);

	if(!func){
		HWTEST_INFO_FUNC("func pointer is null!\n");
		return;
	}

	for(i = 0; i < func->card->num_info;i++){

		if(g_read_card_info_bysdio[i]){
			kfree(g_read_card_info_bysdio[i]);
		}
	}

	kfree(g_read_card_info_bysdio);
	
	HWTEST_INFO_FUNC("%s:sdio func(0x%p) is removed successfully!\n", __FUNCTION__, func);
	
	HWTEST_INFO_FUNC("%s end\n", __FUNCTION__);
}


static int __init hw_sdio_init(void)
{
    int   ret = 0;
	
	HWTEST_INFO_FUNC("%s start \n",__FUNCTION__);

    //register to mmc driver
    ret = sdio_register_driver(&hw_sdio_client_drv);
	
	HWTEST_INFO_FUNC("sdio_register_driver() ret=%d\n", ret);
	HWTEST_INFO_FUNC("%s end\n", __FUNCTION__);

    return ret;
}

static void __exit hw_sdio_exit(void)
{
	int   ret = 0;

    HWTEST_INFO_FUNC("%s start \n", __FUNCTION__);

    sdio_unregister_driver(&hw_sdio_client_drv);


    HWTEST_INFO_FUNC("%s end\n", __FUNCTION__);
    return;
} 

module_init(hw_sdio_init);
module_exit(hw_sdio_exit);

