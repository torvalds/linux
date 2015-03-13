/* Copyright (c) 2014 - 2015 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 */
#include <linux/module.h>
#include "esp_mem.h"
#include "esp_slab.h"
#include "esp_log.h"
#include "version.h"

#define RETRY_COUNT 10

static int __init esp_mem_init(void)
{
	int err = 0;
	int retry;

	logi("%s enter date %s %s\n", __func__, __DATE__, __TIME__);
	logi("%s VERSION [%s]\n", __func__, PREALLOC_VERSION);

#ifdef ESP_SLAB
	retry = RETRY_COUNT;
	do {
		err = esp_slab_init();
		if (err) 
			loge("%s esp_slab_init failed %d, retry %d\n", __func__, err, retry);
		else 
			break;

	} while (--retry > 0);

	if (retry <= 0)
		goto _err_slab;
#endif

#ifdef ESP_PRE_MEM
	retry = RETRY_COUNT;
	do {
		err = esp_indi_pre_mem_init();
		if (err)
			loge("%s esp_indi_pre_mem__init failed %d, retry %d\n", __func__, err, retry);
		else 
			break;

	} while (--retry > 0);

	if (retry <= 0)
		goto _err_mem;
#endif
	logi("%s complete \n", __func__);
	return 0;

#ifdef ESP_PRE_MEM
_err_mem:
#endif
#ifdef ESP_SLAB
	esp_slab_deinit();
_err_slab:
#endif
	return err;	
}

static void __exit esp_mem_exit(void)
{
	logi("%s enter \n", __func__);
#ifdef ESP_SLAB
	esp_slab_deinit();
#endif
#ifdef ESP_PRE_MEM
	esp_indi_pre_mem_deinit();
#endif
	logi("%s complete \n", __func__);
}


module_init(esp_mem_init);
module_exit(esp_mem_exit);
