#include "cf_protocol.h"
#include "cf_port.h"
#include "../ata/ata_protocol.h"

//Global struct variable, to hold all card information need to operate card
CF_Card_Info_t _ata_cf_info = {0,		//blk_len
							   0,		//blk_nums
							   0,		//inited_flag
							   0,		//removed_flag
							   0,		//init_retry
							   0,		//dev_no
							   0,		//addr_mode
							   {0},		//serial_number
							   {0},		//model_number
							   NULL,	//ata_cf_power
							   NULL,	//ata_cf_reset
							   NULL,	//ata_cf_get_ins
							   NULL     //ata_cf_io_release					 
							  };
CF_Card_Info_t *ata_cf_info = &_ata_cf_info;

extern unsigned char card_in_event_status[CARD_MAX_UNIT];

unsigned char cf_m2_enabled = 0;
unsigned cf_power_delay = 0;
unsigned cf_retry_init = 0;

void cf_hw_reset(void);
void cf_staff_init(void);
int cf_cmd_test(void);
unsigned atapi_enable_status =0;
unsigned atapi_output_status =0;
void save_atapi_io_status(void)
{
#if (defined AML_NIKE || defined AML_NIKED) || defined(AML_NIKED3)
//    atapi_enable_status = *(volatile unsigned *)CARD_GPIO_ENABLE;
//    atapi_output_status = *(volatile unsigned *)CARD_GPIO_OUTPUT;
#else
    atapi_enable_status = *(volatile unsigned *)ATAPI_GPIO_ENABLE;
    atapi_output_status = *(volatile unsigned *)ATAPI_GPIO_OUTPUT;
#endif
}

void set_atapi_io_low(void)
{
    cf_set_reset_low();
#if (defined AML_NIKE || defined AML_NIKED) || defined(AML_NIKED3)
//    *(volatile unsigned *)CARD_GPIO_ENABLE &= 0xfc800000;
//    *(volatile unsigned *)CARD_GPIO_OUTPUT &= 0xfc800000;
#else
    *(volatile unsigned *)ATAPI_GPIO_ENABLE &= 0xfc800000;
    *(volatile unsigned *)ATAPI_GPIO_OUTPUT &= 0xfc800000;
#endif
}
   
void restore_atapi_io_status(void)
{
#if (defined AML_NIKE || defined AML_NIKED) || defined(AML_NIKED3)
//    *(volatile unsigned *)CARD_GPIO_ENABLE = atapi_enable_status;
//    *(volatile unsigned *)CARD_GPIO_OUTPUT = atapi_output_status;
#else
    *(volatile unsigned *)ATAPI_GPIO_ENABLE = atapi_enable_status;
    *(volatile unsigned *)ATAPI_GPIO_OUTPUT = atapi_output_status;
#endif
}

int cf_card_init(CF_Card_Info_t * card_info)
{
	int error, dev;
	
	//cf_atapi_enable();
	//set_atapi_enable(0,cf_m2_enabled);
	//set_atapi_enable(0,0);
	if(ata_cf_info->inited_flag && !ata_cf_info->removed_flag)
	{
		cf_atapi_enable();
		
		error = cf_cmd_test();
		if(!error)
			return error;
	}
	
	if(++ata_cf_info->init_retry > CF_INIT_RETRY)
		return ATA_ERROR_HARDWARE_FAILURE;
	
#ifdef ATA_DEBUG
	Debug_Printf("\nCF initialization started......\n");
#endif

    //save_atapi_io_status();
   //if(cf_retry_init)
    	//set_atapi_io_low();

	cf_staff_init();

#ifdef ATA_DEBUG
	Debug_Printf("Start to power on and hardware reset...\n");
#endif
	cf_hw_reset();

	cf_atapi_enable();
	
	//restore_atapi_io_status();
	
	error = ata_init_device(ata_dev);
			
	if(error)
	{
#ifdef ATA_DEBUG
		Debug_Printf("#%s occured in ata_init_device()!\n", ata_error_to_string(error));
#endif
		return error;
	}

	card_in_event_status[CARD_COMPACT_FLASH] = CARD_EVENT_INSERTED;
	
	error = ata_identify_device(ata_dev);
	if(error)
	{
#ifdef ATA_DEBUG
		Debug_Printf("#%s occured in ata_identify_device()!\n", ata_error_to_string(error));
#endif
		return error;
	}

	for(dev=0; dev<2; dev++)
	{
		if(!ata_dev->device_info[dev].device_existed || !ata_dev->device_info[dev].device_inited)
			continue;
			
		//if((ata_dev->device_info[dev].identify_info.general_configuration != 0x848A)
		//	&& !(ata_dev->device_info[dev].identify_info.general_configuration & 0x0080))	// Bit7: removable media device
		//	continue;
			
		ata_cf_info->inited_flag = 1;
		ata_cf_info->dev_no = dev;
		
		break;
	}
	if(!ata_cf_info->inited_flag)
	{
		ata_remove_device(ata_dev, ata_cf_info->dev_no);
		error = ATA_ERROR_DEVICE_TYPE;
#ifdef ATA_DEBUG
		Debug_Printf("#%s occured in cf_card_init()!\n", ata_error_to_string(error));
#endif
		return error;
	}

	strcpy(ata_cf_info->serial_number, ata_dev->device_info[ata_cf_info->dev_no].serial_number);
	strcpy(ata_cf_info->model_number, ata_dev->device_info[ata_cf_info->dev_no].model_number);
#ifdef ATA_DEBUG
	Debug_Printf("ATA device%d detected!\n", ata_cf_info->dev_no);
	Debug_Printf("Serial Number: %s\n", ata_cf_info->serial_number);
	Debug_Printf("Model Number: %s\n", ata_cf_info->model_number);
#endif

	ata_cf_info->blk_len = ata_dev->device_info[ata_cf_info->dev_no].sector_size;
	ata_cf_info->blk_nums = ata_dev->device_info[ata_cf_info->dev_no].sector_nums;
	
	ata_dev->current_dev = ata_cf_info->dev_no;
	ata_dev->current_addr_mode = ata_cf_info->addr_mode;
	
	error = ata_check_data_consistency(ata_dev);
	if(error)
	{
		ata_remove_device(ata_dev, ata_cf_info->dev_no);
		ata_cf_info->inited_flag = 0;
		
#ifdef ATA_DEBUG
		Debug_Printf("#%s occured in ata_check_data_consistency()!\n", ata_error_to_string(error));
#endif
		return error;
	}

#ifdef ATA_DEBUG
	Debug_Printf("cf_card_init() is completed successfully!\n\n");
#endif
	
	ata_cf_info->inited_flag = 1;
	ata_cf_info->init_retry = 0;
	
	memcpy(card_info, ata_cf_info, sizeof(CF_Card_Info_t));

	return ATA_NO_ERROR;
}

int cf_check_insert(void)
{
	int level;
	
	if(ata_cf_info->ata_cf_get_ins)
	{
		level = ata_cf_info->ata_cf_get_ins();
	}
	else
	{
		cf_set_ins_input();
		level = cf_get_ins_value();
	}
	
	if (level)
	{
		if(ata_cf_info->init_retry)
		{
			cf_power_off();
			ata_cf_info->init_retry = 0;
		}
			
		if(ata_cf_info->inited_flag)
		{
			cf_power_off();
			ata_cf_info->removed_flag = 1;
			ata_cf_info->inited_flag = 0;
			
			ata_remove_device(ata_dev, ata_cf_info->dev_no);
		}
			
		return 0;       //No card is inserted
	}
	else
	{
		return 1;       //A card is inserted
	}
}

int cf_read_data(unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error;
	
	cf_atapi_enable();
	
	ata_dev->current_dev = ata_cf_info->dev_no;
	ata_dev->current_addr_mode = ata_cf_info->addr_mode;
	
	error = ata_read_data_pio(lba, byte_cnt, data_buf);
	if(error)
	{
#ifdef ATA_DEBUG
		Debug_Printf("#%s occured in cf_read_data()!\n", ata_error_to_string(error));
#endif
		return error;
	}
	
	return ATA_NO_ERROR;
}

int cf_write_data(unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error;
	
	cf_atapi_enable();
	
	ata_dev->current_dev = ata_cf_info->dev_no;
	ata_dev->current_addr_mode = ata_cf_info->addr_mode;
	
	error = ata_write_data_pio(lba, byte_cnt, data_buf);
	if(error)
	{
#ifdef ATA_DEBUG
		Debug_Printf("#%s occured in cf_write_data()!\n", ata_error_to_string(error));
#endif
		return error;
	}
	
	return ATA_NO_ERROR;
}

void cf_power_on(void)
{
	ata_delay_ms(cf_power_delay+1);

	if(card_share_ins_pwr_flag[CARD_MODULE_CF])
		card_power_off_flag[CARD_COMPACT_FLASH] = 1;

#ifdef CF_POWER_CONTROL
	if(ata_cf_info->ata_cf_power)
	{
		if(cf_check_insert())
		{
			ata_cf_info->ata_cf_power(1);
		}
	}
	else
	{
		if(cf_check_insert())
		{
			cf_set_enable();
		}
	}
#endif
}

void cf_power_off(void)
{
#ifdef CF_POWER_CONTROL
	if(ata_cf_info->ata_cf_power)
	{
		ata_cf_info->ata_cf_power(0);
	}
	else
	{
		cf_set_disable();
	}
#endif

	if(card_share_ins_pwr_flag[CARD_MODULE_CF])
		card_power_off_flag[CARD_COMPACT_FLASH] = 0;
}

void cf_hw_reset()
{
/*	if(ata_cf_info->ata_cf_reset)
//	{
		ata_cf_info->ata_cf_reset(1);
	}
	else
	{	
		cf_set_reset_output();
		cf_set_reset_high();
	}
*/
#ifdef CF_POWER_CONTROL
	cf_power_off();
#endif
	ata_delay_ms(200);
#ifdef CF_POWER_CONTROL
	cf_power_on();
	ata_delay_ms(300);
#endif
	
	if(ata_cf_info->ata_cf_reset)
	{
		ata_cf_info->ata_cf_reset(0);
	}
	else	
	{
		cf_set_reset_output();
		cf_set_reset_low();
	}
	ata_delay_ms(100);
	if(ata_cf_info->ata_cf_reset)
	{
		ata_cf_info->ata_cf_reset(1);
	}
	else
	{
		cf_set_reset_output();
		cf_set_reset_high();
	}
	ata_delay_ms(200);
}

void cf_staff_init(void)
{
	ata_cf_info->addr_mode = ATA_LBA_MODE;
	ata_dev->current_addr_mode = ata_cf_info->addr_mode;
}

int cf_cmd_test(void)
{
	ata_dev->current_dev = ata_cf_info->dev_no;
	ata_dev->current_addr_mode = ata_cf_info->addr_mode;
	
	return ata_check_cmd_validity(ata_dev);
}

void cf_prepare_init(void)
{
	if(cf_power_register != NULL)
		ata_cf_info->ata_cf_power = cf_power_register;
	if(cf_reset_register != NULL)
		ata_cf_info->ata_cf_reset = cf_reset_register;
	if(cf_ins_register != NULL)
		ata_cf_info->ata_cf_get_ins = cf_ins_register;
	if(cf_io_release_register != NULL)
		ata_cf_info->ata_cf_io_release = cf_io_release_register;
}

/*void cf_get_info(blkdev_stat_t *info)
{
	if(info->magic != BLKDEV_STAT_MAGIC)
		return;
	info->valid = 1;
	info->blk_size = ata_cf_info->blk_len;
	info->blk_num = ata_cf_info->blk_nums;
	info->blkdev_name = "CF card";
}*/

void cf_exit(void)
{
	//set_atapi_enable(0,0);
	//restore_atapi_io_status();
}
