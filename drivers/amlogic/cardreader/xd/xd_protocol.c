#include "xd_port.h"
#include "xd_misc.h"
#include "xd_sm.h"
#include "xd_protocol.h"
#include "xd_enc.h"


unsigned xd_power_delay = 0;
extern unsigned char xd_reset_flag_read, xd_reset_flag_write;

//Check if any card is inserted
int xd_check_insert()
{
	int level;
	
	if(xd_sm_info->xd_get_ins)
	{
		level = xd_sm_info->xd_get_ins();
	}
	else
	{	
		xd_set_ins_input();
	    level = xd_get_ins_value();
	}
	
	if(level)
	{
		if(xd_sm_info->xd_init_retry)
		{
			xd_sm_power_off();
			xd_sm_info->xd_init_retry = 0;
		}
			
		if(xd_sm_info->xd_inited_flag)
		{
			xd_sm_power_off();
			xd_sm_info->xd_removed_flag = 1;
			xd_sm_info->xd_inited_flag = 0;
		}
			
		return 0;       //No card is inserted
	}
	else
	{
				xd_sm_info->card_type = CARD_TYPE_XD;
				return 1;       //A card is inserted
			}
}

void xd_power_on()
{
	xd_sm_delay_ms(xd_power_delay+1);
	
#ifdef XD_POWER_CONTROL
	if(xd_sm_info->xd_power)
	{
		xd_sm_info->xd_power(0);
	}
	else
	{
		xd_set_disable();
	}
	if ((!xd_reset_flag_read) && (!xd_reset_flag_write))
		xd_sm_delay_ms(100);
	else
	xd_sm_delay_ms(10);
	if(xd_sm_info->xd_power)
	{
		if(xd_check_insert()) //ensure card wasn't removed at this time
		{
		xd_sm_info->xd_power(1);
	}
	}
	else
	{
		if(xd_check_insert()) //ensure card wasn't removed at this time

		{
			xd_set_enable();
		}
	}
	if ((!xd_reset_flag_read) && (!xd_reset_flag_write))
		xd_sm_delay_ms(100);
	else
	xd_sm_delay_ms(10);
#else
	xd_sm_delay_ms(100);
#endif
}

void xd_power_off()
{
#ifdef XD_POWER_CONTROL
	if(xd_sm_info->xd_power)
	{
		xd_sm_info->xd_power(0);
	}
	else
	{
		xd_set_disable();
	}
#endif
}

void xd_io_config()
{
	xd_gpio_enable();
	
	xd_set_rb_input();
	
	xd_set_re_output();
	xd_set_re_disable();

	xd_set_ce_output();
	xd_set_ce_disable();

	xd_set_ale_output();
	xd_set_ale_disable();

	xd_set_cle_output();
	xd_set_cle_disable();

	xd_set_we_output();
	xd_set_we_disable();

	xd_set_wp_output();
	xd_set_wp_enable();

	xd_set_dat0_7_input();
}

void xd_cmd_input_cycle(unsigned char cmd, int enable_write)
{
	xd_set_dat0_7_output();
	
	xd_set_ale_disable();
	xd_set_ce_enable();
	xd_set_cle_enable();
	
	xd_sm_delay_20ns();		// Tcls = Tcs = Tals = 20ns
	
	xd_set_dat0_7_value(cmd);
	
	if(enable_write)
		xd_set_wp_disable();
		
	xd_set_we_enable();
	
	xd_sm_delay_40ns();		// Twp = 40ns, Tds = 30ns
	
	xd_set_we_disable();
	
	xd_sm_delay_40ns();		// Tclh = Tch = Talh = 40ns, Tdh = 20ns
	
	xd_set_ce_disable();
	xd_set_cle_disable();
}

void xd_addr_input_cycle(unsigned long addr, int cycles)
{
	int data,i;
	xd_set_ale_enable();
	xd_set_cle_disable();
	xd_set_ce_enable();
	
	xd_sm_delay_20ns();		// Tcls = Tcs = Tals = 20ns
	
	for(i=0; i<cycles; i++)
	{
		data = (addr >> (i << 3)) & 0xFF;
		xd_set_dat0_7_value(data);
		
		xd_set_we_enable();
		
		xd_sm_delay_40ns();	// Twp = 40ns, Tds = 30ns
		
		xd_set_we_disable();
		
		xd_sm_delay_40ns();	// Twh = 20ns, Tdh = 20ns, Twc = Twp + Twh = 80ns
	}
	
	xd_set_ale_disable();	// Talh = 40ns, Tdh = 20ns
}

void xd_data_input_cycle(unsigned char *data_buf, unsigned long data_cnt, unsigned char *redundant_buf, unsigned long redundant_cnt)
{
	int i;
	
	xd_set_ale_disable();
	
	xd_sm_delay_20ns();		// Tals = 20ns
	
	for(i=0; i<data_cnt; i++)
	{
		xd_set_dat0_7_value(*data_buf++);
		
		xd_set_we_enable();
		
		xd_sm_delay_40ns();	// Twp = 40ns, Tds = 30ns
		
		xd_set_we_disable();
		
		xd_sm_delay_40ns();	// Twh = 20ns, Tdh = 20ns, Twc = Twp + Twh = 80ns
	}
	
	for(i=0; i<redundant_cnt; i++)
	{
		xd_set_dat0_7_value(*redundant_buf++);
		
		xd_set_we_enable();
		
		xd_sm_delay_40ns();	// Twp = 40ns, Tds = 30ns
		
		xd_set_we_disable();
		
		xd_sm_delay_40ns();	// Twh = 20ns, Tdh = 20ns, Twc = Twp + Twh = 80ns
	}
}

void xd_serial_read_cycle(unsigned char *data_buf, unsigned long data_cnt, unsigned char *redundant_buf, unsigned long redundant_cnt)
{
	int i;

#ifdef AMLOGIC_CHIP_SUPPORT
	unsigned long data;

	if((unsigned long)data_buf == 0x3400000)
	{
		for(i=0; i<data_cnt; i++)
		{
			xd_set_re_enable();
			
			xd_sm_delay_60ns();	// Trp = 60ns
			
			data = xd_get_dat0_7_value();
			WRITE_BYTE_TO_FIFO(data);
			
			xd_set_re_disable();
    	
			xd_sm_delay_20ns();	// Treh = 20ns, Trhz = 30ns, Trc = Trp + Treh = 80ns
		}
	}
	else
#endif
	{
		for(i=0; i<data_cnt; i++)
		{
			xd_set_re_enable();
			
			xd_sm_delay_60ns();	// Trp = 60ns
			
			*data_buf++ = xd_get_dat0_7_value();
			
			xd_set_re_disable();
    	
			xd_sm_delay_20ns();	// Treh = 20ns, Trhz = 30ns, Trc = Trp + Treh = 80ns
		}
	}

	for(i=0; i<redundant_cnt; i++)
	{
		xd_set_re_enable();
		
		xd_sm_delay_60ns();	// Trp = 60ns
		
		*redundant_buf++ = xd_get_dat0_7_value();
		
		xd_set_re_disable();

		xd_sm_delay_20ns();	// Treh = 20ns, Trhz = 30ns, Trc = Trp + Treh = 80ns
	}

}

int xd_test_ready()
{
	return xd_get_rb_value();
}

int xd_card_capacity_determin(unsigned char device_code)
{
	unsigned long temp = 0;
	unsigned short cis_search_max = 23;
	char *capacity_str = NULL;

	switch(device_code)
	{
		case 0x73:
			xd_sm_total_zones = ZONE_NUMS_16MB;
			capacity_str = xd_sm_capacity_16MB;
			break;
			
		case 0x75:
			xd_sm_total_zones = ZONE_NUMS_32MB;
			capacity_str = xd_sm_capacity_32MB;
			break;
			
		case 0x76:
			xd_sm_total_zones = ZONE_NUMS_64MB;
			capacity_str = xd_sm_capacity_64MB;
			break;
			
		case 0x79:
			xd_sm_total_zones = ZONE_NUMS_128MB;
			capacity_str = xd_sm_capacity_128MB;
			break;
			
		case 0x71:
			xd_sm_total_zones = ZONE_NUMS_256MB;
			capacity_str = xd_sm_capacity_256MB;
			break;
			
		case 0xDC:
			xd_sm_total_zones = ZONE_NUMS_512MB;
			capacity_str = xd_sm_capacity_512MB;
			break;
			
		case 0xD3:
			xd_sm_total_zones = ZONE_NUMS_1GB;
			capacity_str = xd_sm_capacity_1GB;
			break;
			
		case 0xD5:
			xd_sm_total_zones = ZONE_NUMS_2GB;
			capacity_str = xd_sm_capacity_2GB;
			break;
			
		case 0xD6:
		case 0xD9:
			return XD_SM_ERROR_UNSUPPORTED_CAPACITY;
			
		default:
			return XD_SM_ERROR_DEVICE_ID;
	}
	
#ifdef XD_SM_ALLOC_MEMORY	
	if(!xd_sm_buf)
	{
		xd_sm_buf = (XD_SM_Card_Buffer_t *)xd_sm_malloc(sizeof(XD_SM_Card_Buffer_t),GFP_KERNEL);
		if(xd_sm_buf == NULL)
			return XD_SM_ERROR_NO_MEMORY;
		memset(xd_sm_buf, 0, sizeof(XD_SM_Card_Buffer_t));
	}
#ifdef XD_SM_NUM_POINTER
	xd_sm_actually_zones = xd_sm_total_zones;
	
	if(!xd_sm_buf->logical_physical_table)
	{	
		xd_sm_buf->logical_physical_table = xd_sm_malloc(sizeof(unsigned short)*xd_sm_actually_zones*MAX_LOGICAL_BLKS_PER_ZONE,GFP_KERNEL);
		if(!xd_sm_buf->logical_physical_table)
			return XD_SM_ERROR_NO_MEMORY;
		memset(xd_sm_buf->logical_physical_table, 0, sizeof(unsigned short)*xd_sm_actually_zones*MAX_LOGICAL_BLKS_PER_ZONE);
	}
	if(!xd_sm_buf->free_block_table)	
	{
		xd_sm_buf->free_block_table = xd_sm_malloc(sizeof(unsigned char)*xd_sm_actually_zones*MAX_PHYSICAL_BLKS_PER_ZONE / 8,GFP_KERNEL);
		if(!xd_sm_buf->free_block_table)
			return XD_SM_ERROR_NO_MEMORY;
		memset(xd_sm_buf->free_block_table, 0, sizeof(unsigned char)*xd_sm_actually_zones*MAX_PHYSICAL_BLKS_PER_ZONE / 8);
	}
#endif
#endif
	
	xd_sm_buf->capacity_str = capacity_str;
	xd_sm_buf->cis_search_max = cis_search_max;
	
	xd_sm_physical_blks_perzone = 1024;
	xd_sm_logical_blks_perzone = 1000;
	xd_sm_pages_per_blk = 32;
	xd_sm_page_size = 512;
	xd_sm_redundant_size = 16;
	xd_sm_buf->addr_cycles = 4;

	temp = xd_sm_total_zones;
	temp *= xd_sm_physical_blks_perzone;
	xd_sm_totoal_physical_blks = temp;
	
	temp = xd_sm_total_zones;
	temp *= xd_sm_logical_blks_perzone;
	xd_sm_totoal_logical_blks = temp;
	
	return XD_SM_NO_ERROR;
}

void xd_exit()
{
	if(xd_sm_info->xd_io_release){
		xd_sm_info->xd_io_release();
        //AVDebug_OS_Irq_Check(NULL);
	}	
	
#ifdef XD_SM_ALLOC_MEMORY	
	if((!xd_sm_info->sm_inited_flag) && (!xd_sm_info->sm_init_retry))
	{	
		if(xd_sm_buf)
		{
#ifdef XD_SM_NUM_POINTER
			if(xd_sm_buf->logical_physical_table)
			{
				xd_sm_free(xd_sm_buf->logical_physical_table);

           // AVDebug_OS_Irq_Check(NULL);
		    
				xd_sm_buf->logical_physical_table = NULL;

           // AVDebug_OS_Irq_Check(NULL);

			}
			if(xd_sm_buf->free_block_table)
			{
				xd_sm_free(xd_sm_buf->free_block_table);

           // AVDebug_OS_Irq_Check(NULL);

				xd_sm_buf->free_block_table = NULL;

           // AVDebug_OS_Irq_Check(NULL);

			}
#endif	
			xd_sm_free(xd_sm_buf);	

           // AVDebug_OS_Irq_Check(NULL);

			xd_sm_buf = NULL;

           // AVDebug_OS_Irq_Check(NULL);
	
		}
	}
#endif	
}

void xd_prepare_init()
{
	//if(xd_power_register)
	//	xd_sm_info->xd_power = xd_power_register;
	//if(xd_ins_register)
	//	xd_sm_info->xd_get_ins = xd_ins_register;	
	//if(xd_io_release_register)
	//	xd_sm_info->xd_io_release = xd_io_release_register;		
}
