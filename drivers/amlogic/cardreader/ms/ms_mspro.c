#include <asm/cacheflush.h>
#include <linux/pagemap.h>
#include "ms_misc.h"
#include "ms_port.h"

/*
//Global and static variable definitions
static MS_MSPRO_Card_Info_t _ms_mspro_info = {CARD_NONE_TYPE,       //card_type
											  MEMORY_STICK_ERROR,   //media_type
											  INTERFACE_SERIAL,     //interface_mode
											  0,                    //blk_len
											  0,                    //blk_nums
											  0,                    //write_protected_flag
											  0,                    //read_only_flag
											  0,                    //inited_flag
											  0,                    //removed_flag
										      0,					//init_retry
										      0,					//raw_cid
										      55,					//ms_clk_unit(defaule 55ns)
										      NULL,					//ms_mspro_power
										      NULL,					//ms_mspro_get_ins
										      NULL					//ms_mspro_io_release
											 };
*/
static unsigned long ms_save_hw_io_config;
static unsigned long ms_save_hw_io_mult_config;
static unsigned long ms_save_hw_reg_flag;

unsigned char disable_port_switch = 0;
unsigned ms_mspro_power_delay = 0;
unsigned check_one_boot_block = 0;
unsigned ms_force_write_two_times = 0;
unsigned mspro_access_status_reg_after_read = 0;

//extern unsigned ms_power_off_lost_data_flag;
extern unsigned char card_in_event_status[CARD_MAX_UNIT];
extern unsigned sdio_timeout_int_times;

static char * ms_mspro_error_string[]={
				"MS_MSPRO_NO_ERROR",
				"MS_MSPRO_ERROR_TPC_FORMAT",
				"MS_MSPRO_ERROR_RDY_TIMEOUT",
				"MS_MSPRO_ERROR_INT_TIMEOUT",
				"MS_MSPRO_ERROR_DATA_CRC",
				"MS_MSPRO_ERROR_MEDIA_TYPE",
				"MS_MSPRO_ERROR_CMDNK",
				"MS_MSPRO_ERROR_CED",
				"MS_MSPRO_ERROR_FLASH_READ",
				"MS_MSPRO_ERROR_FLASH_WRITE",
				"MS_MSPRO_ERROR_FLASH_ERASE",
				"MS_MSPRO_ERROR_PARAMETER",
				"MS_MSPRO_ERROR_WRITE_PROTECTED",
				"MS_MSPRO_ERROR_READ_ONLY",
				"MS_ERROR_BOOT_SEARCH",
				"MS_ERROR_MEMORY_STICK_TYPE",
				"MS_ERROR_FORMAT_TYPE",
				"MS_ERROR_BLOCK_NUMBER_SIZE",
				"MS_ERROR_DISABLED_BLOCK",
				"MS_ERROR_NO_FREE_BLOCK",
				"MS_ERROR_LOGICAL_PHYSICAL_TABLE",
				"MS_ERROR_BOOT_IDI",
				"MSPRO_ERROR_MEDIA_BREAKDOWN",
				"MSPRO_ERROR_STARTUP_TIMEOUT",
				"MSPRO_ERROR_WRITE_DISABLED",
#ifdef MS_MSPRO_HW_CONTROL
				"MS_MSPRO_ERROR_TIMEOUT",
				"MS_MSPRO_ERROR_UNSUPPORTED",
#endif
                "MS_MSPRO_ERROR_NO_MEMORY",
                "MS_MSPRO_ERROR_NO_READ"
				};

unsigned short mass_counter;

//Functions only used in this source file]
void ms_mspro_prepare_power(MS_MSPRO_Card_Info_t *ms_mspro_info);
void ms_mspro_io_config(MS_MSPRO_Card_Info_t *ms_mspro_info);
int ms_mspro_staff_init(MS_MSPRO_Card_Info_t *ms_mspro_info);
int ms_mspro_cmd_test(MS_MSPRO_Card_Info_t *ms_mspro_info);
int ms_mspro_media_type_identification(MS_MSPRO_Card_Info_t *ms_mspro_info);
MS_MSPRO_Media_Type_t ms_mspro_check_media_type(MS_MSPRO_Card_Info_t *ms_mspro_info);
int ms_mspro_interface_mode_switching(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_Interface_Mode_t new_interface_mode);

int ms_mspro_check_data_consistency(MS_MSPRO_Card_Info_t *ms_mspro_info);

//Function implement start...

//Return the string buf address of specific errcode
char *ms_mspro_error_to_string(int errcode)
{
	return ms_mspro_error_string[errcode];
}

#ifdef MS_MSPRO_HW_CONTROL
int ms_mspro_wait_int_hw(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet)
{
	unsigned long temp = 0;
	
	unsigned long irq_config = 0;
	
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_start_timer(MS_MSPRO_INT_TIMEOUT);
		do
		{
			irq_config = READ_CBUS_REG(SDIO_IRQ_CONFIG);
			temp = (irq_config >> 8) & 0x0000000F;
			
			if(temp)
			{
				((MS_MSPRO_INT_Register_t *)&tpc_packet->int_reg)->CED = (temp&0x01)?1:0;   //DAT0
				((MS_MSPRO_INT_Register_t *)&tpc_packet->int_reg)->ERR = (temp&0x02)?1:0;   //DAT1
				((MS_MSPRO_INT_Register_t *)&tpc_packet->int_reg)->BREQ = (temp&0x04)?1:0;  //DAT2
				((MS_MSPRO_INT_Register_t *)&tpc_packet->int_reg)->CMDNK = (temp&0x08)?1:0; //DAT3
				
				break;
			}

		} while(!ms_check_timer());
	}
	else // Serial I/F
	{
		ms_start_timer(MS_MSPRO_INT_TIMEOUT);
		do
		{
			irq_config = READ_CBUS_REG(SDIO_IRQ_CONFIG);
			temp = (irq_config >> 8) & 0x00000001;
			
			if(temp)
				break;

		} while(!ms_check_timer());
	}
	
	if(ms_check_timeout())
		return MS_MSPRO_ERROR_INT_TIMEOUT;
	else
		return MS_MSPRO_NO_ERROR;
}
#endif

#ifdef MS_MSPRO_SW_CONTROL
int ms_mspro_wait_int_sw(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet)
{
	unsigned long temp = 0, data = 0, cnt = 0;
	
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_start_timer(MS_MSPRO_INT_TIMEOUT);
		do
		{
			ms_clk_parallel_high();
		
			data = ms_get_dat0_3_value();
			
			ms_clk_parallel_low();
			
			if(data)
			{
				if(data != temp)
				{
					temp = data;
					cnt = 0;
					continue;
				}
				else
				{
					if(++cnt < 10)
						continue;
				}

				((MS_MSPRO_INT_Register_t *)&tpc_packet->int_reg)->CED = (temp&0x01)?1:0;   //DAT0
				((MS_MSPRO_INT_Register_t *)&tpc_packet->int_reg)->ERR = (temp&0x02)?1:0;   //DAT1
				((MS_MSPRO_INT_Register_t *)&tpc_packet->int_reg)->BREQ = (temp&0x04)?1:0;  //DAT2
				((MS_MSPRO_INT_Register_t *)&tpc_packet->int_reg)->CMDNK = (temp&0x08)?1:0; //DAT3
				
				break;
			}

		} while(!ms_check_timer());
	}
	else // Serial I/F
	{
		ms_start_timer(MS_MSPRO_INT_TIMEOUT);
		do
		{
			ms_clk_serial_low();
			
			data = ms_get_dat0_value();
			
			ms_clk_serial_high();
			
			if(data)
				break;

		} while(!ms_check_timer());
	}
	
	if(ms_check_timeout())
		return MS_MSPRO_ERROR_INT_TIMEOUT;
	else
		return MS_MSPRO_NO_ERROR;
}
#endif

int ms_mspro_wait_rdy(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet)
{
	unsigned long temp = 0, data;
	
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_clk_parallel_high();
		ms_clk_parallel_low();
		
		ms_start_timer(MS_MSPRO_RDY_TIMEOUT);
		do
		{
			ms_clk_parallel_high();
		
			data = ms_get_dat0_value();
			temp <<= 1;
			temp |= data;
		
			ms_clk_parallel_low();
		
			if(((temp&0x3f) == 0x15)||      // 010101b
			((temp&0x3f) == 0x2A))          // 101010b
			{
				break;
			}
		
		} while(!ms_check_timer());
	
		if(tpc_packet->TPC_cmd.trans_dir.bWrite)    //Write packet
		{
			ms_set_bs_state(0);
		}
		else
		{
			ms_set_bs_state(3);         //Read packet
		}
	
		ms_clk_parallel_high();
		ms_clk_parallel_low();

		ms_clk_parallel_high();
		ms_clk_parallel_low();
	}
	else
	{
		ms_start_timer(MS_MSPRO_RDY_TIMEOUT);
		do
		{
			ms_clk_serial_low();
		
			data = ms_get_dat0_value();
			temp <<= 1;
			temp |= data;
		
			ms_clk_serial_high();
		
			if(((temp&0x3f) == 0x15)||      // 010101b
			((temp&0x3f) == 0x2A))          // 101010b
			{
				break;
			}
		
		} while(!ms_check_timer());
	
		if(tpc_packet->TPC_cmd.trans_dir.bWrite)    //Write packet
		{
			ms_set_bs_state(0);
		}
		else
		{
			ms_set_bs_state(3);         //Read packet
		}
	
		ms_clk_serial_low();
		ms_clk_serial_high();

	}
	
	if(ms_check_timeout())
		return MS_MSPRO_ERROR_RDY_TIMEOUT;
	else
		return MS_MSPRO_NO_ERROR;
}

int ms_mspro_write_tpc(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet)
{
	int i;
	unsigned long temp,tpc_cmd;
	
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_clk_parallel_low();
		ms_set_bs_state(1);             //set bus state to BS1
		ms_clk_parallel_high();
		ms_clk_parallel_low();
	
		ms_clk_parallel_high();
		ms_set_dat0_3_input();              //Z
		ms_clk_parallel_low();

		ms_clk_parallel_high();
		
		ms_clk_parallel_low();
		ms_set_dat0_3_output();             //X
		ms_set_dat0_3_value(0);
		ms_clk_parallel_high();
		ms_clk_parallel_low();
						
		temp = tpc_packet->TPC_cmd.format.code;     //TPC code
		ms_set_dat0_3_value(temp);
		ms_clk_parallel_high();
		ms_clk_parallel_low();
				
		temp = tpc_packet->TPC_cmd.format.check_code;   //TPC check code
		ms_set_dat0_3_value(temp);
		ms_clk_parallel_high();
		ms_clk_parallel_low();
		
		ms_set_bs_state(2);             //set bus state to BS2
		
		ms_set_dat0_3_value(0);
		ms_clk_parallel_high();
		ms_clk_parallel_low();

		if (tpc_packet->TPC_cmd.trans_dir.bRead)    //Read packet
				ms_set_dat0_3_input();
				
		ms_clk_parallel_high();
		ms_clk_parallel_low();
	}
	else
	{
		ms_clk_serial_high();
		ms_set_bs_state(1);                 //set bus state to BS1
		ms_clk_serial_low();
		ms_clk_serial_high();
	
		ms_set_dat0_output();
		
		tpc_cmd = tpc_packet->TPC_cmd.value;
		for(i=0; i<8; i++)
		{
			temp = (tpc_cmd >> (8 - i - 1)) & 0x01;
			ms_set_dat0_value(temp);
			
			if(i == 7)
				ms_set_bs_state(2);     //set bus state to BS2
			
			ms_clk_serial_low();
			ms_clk_serial_high();
		}
		
		if (tpc_packet->TPC_cmd.trans_dir.bRead)    //Read packet
			ms_set_dat0_input();
	}
	
	return MS_MSPRO_NO_ERROR;
}

int ms_mspro_read_data_line(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet)
{
	unsigned long data_cnt, temp = 0, loop_num, data;
	unsigned short crc16 = 0;

	int i;
	
#ifdef MS_MSPRO_CRC_CHECK
	unsigned short crc_check = 0; int error=0;
#endif

	unsigned char * data_buf = tpc_packet->param.in.buffer;
	
	loop_num = tpc_packet->param.in.count;
	
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
#ifdef AMLOGIC_CHIP_SUPPORT
		if((unsigned long)data_buf == 0x3400000)
		{
			for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
			{
				temp = 0;
		
				for(i = 0; i < 2; i++)
				{
					ms_clk_parallel_high();
						
					data = ms_get_dat0_3_value();
					temp <<= 4;
					temp |= data;

					ms_clk_parallel_low();
				}
			
				WRITE_BYTE_TO_FIFO(temp);
				
#ifdef MS_MSPRO_CRC_CHECK
				crc_check = (crc_check << 8) ^ ms_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
			}
		}
		else
#endif
		{
			for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
			{
				temp = 0;
		
				for(i = 0; i < 2; i++)
				{
					ms_clk_parallel_high();
						
					data = ms_get_dat0_3_value();
					temp <<= 4;
					temp |= data;

					ms_clk_parallel_low();
				}
			
				*data_buf = temp;
				data_buf++;
				
#ifdef MS_MSPRO_CRC_CHECK
				crc_check = (crc_check << 8) ^ ms_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
			}
		}
	
		for(i = 0; i < 4; i++)
		{
			ms_clk_parallel_high();
			
			data = ms_get_dat0_3_value();
			crc16 <<= 4;
			crc16 |= data;
			
			ms_clk_parallel_low();
			
			if(data_cnt == 2)
				ms_set_bs_state(0);
		}
	}
	else // Serial I/F
	{
#ifdef AMLOGIC_CHIP_SUPPORT
		if((unsigned long)data_buf == 0x3400000)
		{
			for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
			{
				temp = 0;
			
				for(i = 0; i < 8; i++)
				{
					ms_clk_serial_low();
						
					data = ms_get_dat0_value();
					temp <<= 1;
					temp |= data;
			
					ms_clk_serial_high();
				}
				
				WRITE_BYTE_TO_FIFO(temp);
				
#ifdef MS_MSPRO_CRC_CHECK
				crc_check = (crc_check << 8) ^ ms_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
			}
		}
		else
#endif
		{
			for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
			{
				temp = 0;
			
				for(i = 0; i < 8; i++)
				{
					ms_clk_serial_low();
						
					data = ms_get_dat0_value();
					temp <<= 1;
					temp |= data;
			
					ms_clk_serial_high();
				}
				
				*data_buf = temp;
				data_buf++;
				
#ifdef MS_MSPRO_CRC_CHECK
				crc_check = (crc_check << 8) ^ ms_crc_table[((crc_check >> 8) ^ temp) & 0xff];
#endif
			}
		}
	
		for(i = 0; i < 16; i++)
		{
			if(i == 15)
				ms_set_bs_state(0);
			
			ms_clk_serial_low();
			
			data = ms_get_dat0_value();
			crc16 <<= 1;
			crc16 |= data;

			ms_clk_serial_high();
		}
	}
	
#ifdef MS_MSPRO_CRC_CHECK
	if(crc16 != crc_check)
	{
		error = MS_MSPRO_ERROR_DATA_CRC;
		
		#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_read_data_line()!\n", ms_mspro_error_to_string(error));
		#endif
        return error;
	}
#endif
	
	return MS_MSPRO_NO_ERROR;
}

int ms_mspro_write_data_line(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet)
{
	unsigned long data_cnt, loop_num, data;
	unsigned short crc16 = 0;
	
	int i;
	
	unsigned char * data_buf = tpc_packet->param.out.buffer;
	//unsigned char * org_buf = tpc_packet->param.out.buffer;
	
	loop_num = tpc_packet->param.out.count;
	
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
			//Write data
		for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
		{
			for(i = 1; i >= 0; i--)
			{
				data = (*data_buf >> (i<<2)) & 0x0F;
				
				ms_set_dat0_3_value(data);
			
				ms_clk_parallel_high();
				ms_clk_parallel_low();
			}
				
				crc16 = (crc16 << 8) ^ ms_crc_table[((crc16 >> 8) ^ *data_buf) & 0xFF];
				data_buf++;
		}
		
		//Caculate CRC16 value and write to line
		//crc16 = ms_cal_crc16(org_buf, tpc_packet->param.out.count);
		
		//Write CRC16
		for(i = 3; i >= 0; i--)
		{
			data = (crc16 >> (i<<2)) & 0x000F;
			ms_set_dat0_3_value(data);
			
			ms_clk_parallel_high();
			ms_clk_parallel_low();
		}
		
		ms_set_bs_state(3);

		ms_set_dat0_3_value(0);
		ms_clk_parallel_high();
		ms_clk_parallel_low();

		ms_set_dat0_3_input();
		
		ms_clk_parallel_high();
		ms_clk_parallel_low();
	}
	else // Serial I/F
	{
		//Write data
		for(data_cnt = 0; data_cnt < loop_num; data_cnt++)
		{
			for(i = 7; i >= 0; i--)
			{
				data = (*data_buf >> i) & 0x01;
				
				ms_set_dat0_value(data);
			
				ms_clk_serial_low();
				ms_clk_serial_high();
			}
	
			crc16 = (crc16 << 8) ^ ms_crc_table[((crc16 >> 8) ^ *data_buf) & 0xFF];
			data_buf++;
		}
		
		//Caculate CRC16 value and write to line
		//crc16 = ms_cal_crc16(org_buf, tpc_packet->param.out.count);
		
		//Write CRC16
		for(i = 15; i >= 0; i--)
		{
			data = (crc16 >> i) & 0x0001;
			ms_set_dat0_value(data);
			
			if(i == 0)
			{
				ms_set_bs_state(3);
			}
			
			ms_clk_serial_low();
			ms_clk_serial_high();
		}

		ms_set_dat0_input();
	}

	return MS_MSPRO_NO_ERROR;
}

#ifdef MS_MSPRO_HW_CONTROL
int ms_mspro_packet_communicate_hw(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet)
{
	int num_res,i,cnt;
	unsigned char *data_buf;
	unsigned long cmd_arg,cmd_send,cmd_ext;
	unsigned long timeout, repeat_time;
	unsigned long status_irq;
	//unsigned long irq_config;	
	MSHW_CMD_Send_Reg_t *cmd_send_reg;
	MSHW_Extension_Reg_t *cmd_ext_reg;
	SDIO_Status_IRQ_Reg_t *status_irq_reg;
	//MSHW_IRQ_Config_Reg_t *irq_config_reg;
	
	num_res = 0;
	data_buf = NULL;
	//check if TPC is valid
	if((tpc_packet->TPC_cmd.format.code ^ tpc_packet->TPC_cmd.format.check_code) != 0x0F)
		return MS_MSPRO_ERROR_TPC_FORMAT;

	cmd_arg = 0;
	cmd_send = 0;
	cmd_ext = 0;
	cmd_send_reg = (void *)&cmd_send;
	cmd_send_reg->tpc_data = tpc_packet->TPC_cmd.value;
	cmd_send_reg->use_int_window = 1;

	cmd_ext_reg = (void *)&cmd_ext;
	
	switch(tpc_packet->TPC_cmd.value)
	{
		case TPC_MS_READ_PAGE_DATA:			//TPC_MSPRO_READ_LONG_DATA
		case TPC_MSPRO_READ_SHORT_DATA:
			//AV_invalidate_dcache();
			//inv_dcache_range((unsigned long)ms_mspro_buffer, ((unsigned long)ms_mspro_buffer + tpc_packet->param.in.count)); 
			//data_buf = ms_mspro_buffer;
			data_buf = ms_mspro_info->data_phy_buf;
			//data_buf = ms_mspro_info->dma_phy_buf;
			cmd_send_reg->have_long_data_read = 1;
			if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
				cmd_ext_reg->long_data_nums = tpc_packet->param.in.count * 8 + 16 - 4;
			else //INTERFACE_SERIAL
				cmd_ext_reg->long_data_nums = tpc_packet->param.in.count * 8 + 16 - 1;
			break;
			
		case TPC_MS_WRITE_PAGE_DATA:		//TPC_MSPRO_WRITE_LONG_DATA
		case TPC_MSPRO_WRITE_SHORT_DATA:
			//if(ms_mspro_info->card_type == CARD_TYPE_MS)
			//	data_buf = ms_mspro_buf->ms.page_buf;
			//else // CARD_TYPE_MSPRO
			//	data_buf = ms_mspro_buf->mspro.sector_buf;
			//memcpy(data_buf, tpc_packet->param.out.buffer, tpc_packet->param.out.count);
			//data_buf = tpc_packet->param.out.buffer;
			dmac_map_area(ms_mspro_info->data_buf, tpc_packet->param.in.count, 1);
			if((tpc_packet->param.out.buffer >= ms_mspro_info->data_buf) &&
				(tpc_packet->param.out.buffer < ms_mspro_info->data_buf+sizeof(MS_MSPRO_Card_Info_t)))
				data_buf = ms_mspro_info->data_phy_buf + 
						((int)(tpc_packet->param.out.buffer) - (int)(ms_mspro_info->data_buf));
			else if((tpc_packet->param.out.buffer >= ms_mspro_info->ms_mspro_buf) &&
				(tpc_packet->param.out.buffer < ms_mspro_info->ms_mspro_buf+PAGE_CACHE_SIZE))
				data_buf = ms_mspro_info->ms_mspro_phy_buf + 
						((int)(tpc_packet->param.out.buffer) - (int)(ms_mspro_info->ms_mspro_buf));
			else if((tpc_packet->param.out.buffer >= ms_mspro_info->dma_buf) &&
				(tpc_packet->param.out.buffer < ms_mspro_info->dma_buf+256*512))
				data_buf = ms_mspro_info->dma_phy_buf + 
						((int)(tpc_packet->param.out.buffer) - (int)(ms_mspro_info->dma_buf));
			else{
				printk("ms_mspro_info->data_buf = %x, ms_mspro_info->data_phy_buf = %x\n"
					"ms_mspro_info->ms_mspro_buf = %x, ms_mspro_info->ms_mspro_phy_buf = %x\n"
					"ms_mspro_info->dma_buf = %x, ms_mspro_info->dma_phy_buf = %x\n"
					"tpc_packet->param.out.buffer = %x, data_buf = %x\n",
					(unsigned int)(ms_mspro_info->data_buf), (unsigned int)(ms_mspro_info->data_phy_buf), 
					(unsigned int)(ms_mspro_info->ms_mspro_buf), (unsigned int)(ms_mspro_info->ms_mspro_phy_buf), 
					(unsigned int)(ms_mspro_info->dma_buf), (unsigned int)(ms_mspro_info->dma_phy_buf), 
					(unsigned int)(tpc_packet->param.out.buffer), (unsigned int)data_buf);
				dump_stack();
				BUG();
			}
			
			cmd_send_reg->have_long_data_write = 1;
			if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
				cmd_ext_reg->long_data_nums = tpc_packet->param.out.count * 8 + 16 - 4;
			else //INTERFACE_SERIAL
				cmd_ext_reg->long_data_nums = tpc_packet->param.out.count * 8 + 16 - 1;
			
			//AV_invalidate_dcache();
			//inv_dcache_range((unsigned long)tpc_packet->param.out.buffer, ((unsigned long)tpc_packet->param.out.buffer + tpc_packet->param.out.count)); 
			break;
			
		case TPC_MS_MSPRO_READ_REG:
		case TPC_MS_MSPRO_GET_INT:
			num_res = tpc_packet->param.in.count;
			if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
				cmd_send_reg->short_data_read_nums = tpc_packet->param.in.count * 8 + 16 - 4;
			else //INTERFACE_SERIAL
				cmd_send_reg->short_data_read_nums = tpc_packet->param.in.count * 8 + 16 - 1;
			break;
			
		case TPC_MS_MSPRO_WRITE_REG:
		case TPC_MS_MSPRO_SET_RW_REG_ADRS:
		case TPC_MS_MSPRO_SET_CMD:
			if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
				cmd_send_reg->short_data_write_nums = tpc_packet->param.out.count * 8 + 16 - 4;
			else //INTERFACE_SERIAL
				cmd_send_reg->short_data_write_nums = tpc_packet->param.out.count * 8 + 16 - 1;

			cnt = tpc_packet->param.out.count;
			for(i=0; i<4; i++)
			{
				if(cnt <= 0)
					break;
					
				cmd_arg |= (unsigned long)(tpc_packet->param.out.buffer[i]) << (24 - i * 8);
				cnt--;
			}
			if(cnt-- > 0)
			{
				cmd_ext_reg->short_data_ext |= tpc_packet->param.out.buffer[i] << 8;
				
				if(cnt)
					cmd_ext_reg->short_data_ext |= tpc_packet->param.out.buffer[i+1];
			}
			break;
			
		case TPC_MSPRO_EX_SET_CMD:
			return MS_MSPRO_ERROR_UNSUPPORTED;
			
		default:
			return MS_MSPRO_ERROR_UNSUPPORTED;
	}

	#define MS_MSPRO_CMD_BUSY_COUNT			7000//7ms
	#define MS_MSPRO_READ_BUSY_COUNT		20000//20ms
	#define MS_MSPRO_WRITE_BUSY_COUNT		20000//20ms
	#define MS_MSPRO_RETRY_COUNT			5

    repeat_time = 0;
    
    if(cmd_send_reg->have_long_data_write)
    	timeout = MS_MSPRO_WRITE_BUSY_COUNT;
    else if(cmd_send_reg->have_long_data_read)
    	timeout = MS_MSPRO_READ_BUSY_COUNT;
    else
    	timeout = MS_MSPRO_CMD_BUSY_COUNT;

//PACKET_RETRY:
	status_irq = 0;
	status_irq_reg = (void *)&status_irq;
	status_irq_reg->if_int = 1;
	status_irq_reg->cmd_int = 1;
	if(timeout > (ms_mspro_info->ms_clk_unit*0x1FFF)/1000)
	{
		status_irq_reg->timing_out_count = 0x1FFF;
		//sdio_timeout_int_times = (timeout*1000)/(ms_mspro_info->ms_clk_unit*0x1FFF);
		sdio_timeout_int_times = timeout/(ms_mspro_info->ms_clk_unit*0x1FFF/1000);
	}
	else
	{
		status_irq_reg->timing_out_count = (ms_mspro_info->ms_clk_unit)*1000;
		sdio_timeout_int_times = 1;
	}
	WRITE_CBUS_REG(SDIO_STATUS_IRQ, status_irq);

	WRITE_CBUS_REG(CMD_ARGUMENT, cmd_arg);
	WRITE_CBUS_REG(SDIO_EXTENSION, cmd_ext);
	if(data_buf != NULL)
	{
		WRITE_CBUS_REG(SDIO_M_ADDR, (unsigned long)data_buf);
	}

	init_completion(&sdio_int_complete);
	sdio_open_host_interrupt(SDIO_CMD_INT);
	sdio_open_host_interrupt(SDIO_TIMEOUT_INT);

	WRITE_CBUS_REG(CMD_SEND, cmd_send);

	//interruptible_sleep_on(&sdio_wait_event);
	wait_for_completion(&sdio_int_complete);
	
	/*timeout_count = 0;
    while(1)
    {
    	status_irq = READ_CBUS_REG(SDIO_STATUS_IRQ);
    	
    	if(!status_irq_reg->cmd_busy && status_irq_reg->cmd_int)
    		break;
    	
        if((++timeout_count) > timeout)
        {
        	irq_config_reg->soft_reset = 1;
            WRITE_CBUS_REG(SDIO_IRQ_CONFIG, irq_config);
            
            if((++repeat_time) > MS_MSPRO_RETRY_COUNT)
        	{
				return MS_MSPRO_ERROR_TIMEOUT;
			}
#ifdef  MS_MSPRO_DEBUG
			//if(cmd_send_reg->have_long_data_write)
			//	Debug_Printf("ms_mspro_packet_communicate() retry %d...\n", repeat_time);
#endif
            goto PACKET_RETRY;
        }
    }*/

	if(sdio_timeout_int_times == 0)
		return MS_MSPRO_ERROR_TIMEOUT;

	status_irq = READ_CBUS_REG(SDIO_STATUS_IRQ);
	if(tpc_packet->TPC_cmd.trans_dir.bRead && !status_irq_reg->data_read_crc16_ok)
		return MS_MSPRO_ERROR_DATA_CRC;
		
	//if(tpc_packet->TPC_cmd.trans_dir.bWrite && !status_irq_reg->data_write_crc16_ok)
	//	return MS_MSPRO_ERROR_DATA_CRC;
	
	if(num_res > 0)
	{
		unsigned long multi_config = 0;
		SDIO_Multi_Config_Reg_t *multi_config_reg = (void *)&multi_config;
		multi_config_reg->write_read_out_index = 1;
		WRITE_CBUS_REG(SDIO_MULT_CONFIG, multi_config);
	}
	while(num_res)
	{
		unsigned long data_temp = READ_CBUS_REG(CMD_ARGUMENT);
		
		tpc_packet->param.in.buffer[--num_res] = data_temp & 0xFF;
		if(num_res <= 0)
			break;
		tpc_packet->param.in.buffer[--num_res] = (data_temp >> 8) & 0xFF;
		if(num_res <= 0)
			break;
		tpc_packet->param.in.buffer[--num_res] = (data_temp >> 16) & 0xFF;
		if(num_res <= 0)
			break;
		tpc_packet->param.in.buffer[--num_res] = (data_temp >> 24) & 0xFF;
	}
	
	if(tpc_packet->TPC_cmd.trans_dir.bRead && data_buf)
	{
#ifdef AMLOGIC_CHIP_SUPPORT
		if((unsigned long)tpc_packet->param.in.buffer == 0x3400000)
		{
			for(i=0; i<tpc_packet->param.in.count; i++)
			{
				WRITE_BYTE_TO_FIFO(data_buf[i]);
			}
		}
		else
#endif
		{
			dmac_map_area(ms_mspro_info->data_buf, tpc_packet->param.in.count, 2);
			memcpy(tpc_packet->param.in.buffer, ms_mspro_info->data_buf, tpc_packet->param.in.count);
			dmac_map_area(ms_mspro_info->data_buf, tpc_packet->param.in.count, 2);	
		}
	}
		
			dmac_map_area(ms_mspro_info->data_buf, tpc_packet->param.in.count, 1);
	return MS_MSPRO_NO_ERROR;
}
#endif

#ifdef MS_MSPRO_SW_CONTROL
int ms_mspro_packet_communicate_sw(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet)
{
	int ret;
	
	//check if TPC is valid
	if((tpc_packet->TPC_cmd.format.code ^ tpc_packet->TPC_cmd.format.check_code) != 0x0F)
		return MS_MSPRO_ERROR_TPC_FORMAT;
		
	//write TPC comand to data line
	ms_mspro_write_tpc(ms_mspro_info, tpc_packet);

	//write data or read response
	if (tpc_packet->TPC_cmd.trans_dir.bWrite)   //Write packet
	{
		ms_mspro_write_data_line(ms_mspro_info, tpc_packet);
			
		ret = ms_mspro_wait_rdy(ms_mspro_info, tpc_packet);
		if(!ret)
		{
			if ((tpc_packet->TPC_cmd.value == TPC_MS_MSPRO_SET_CMD) || (tpc_packet->TPC_cmd.value == TPC_MSPRO_EX_SET_CMD))
				ret = ms_mspro_wait_int(ms_mspro_info, tpc_packet);
		}
	}
	else                        //Read packet
	{
		ret = ms_mspro_wait_rdy(ms_mspro_info, tpc_packet);
		if(!ret)
		{
            ret=ms_mspro_read_data_line(ms_mspro_info, tpc_packet);
		}
	}
	
	ms_set_bs_state(0);
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_clk_parallel_high();
		ms_clk_parallel_low();
	}
	else // Serial I/F
	{
		ms_clk_serial_low();
		ms_clk_serial_high();
	}

	return ret;
}
#endif

int ms_mspro_media_type_identification(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //READ_REG: Status, Type, Catagory, Class
	packet.param.out.count = 4;
	ms_mspro_buf->ms.reg_set.read_addr = 0x02;           //READ_ADRS = 0x02
	ms_mspro_buf->ms.reg_set.read_size = 0x06;           //READ_SIZE = 0x06
	packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	packet.TPC_cmd.value = TPC_MS_MSPRO_READ_REG;       //READ_REG
	packet.param.in.count = 6;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	packet.param.in.buffer = &ms_mspro_buf->ms.regs.Status_Reg0;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_media_type_identification(ms_mspro_info);
	if(error)
	{
		error = mspro_media_type_identification(ms_mspro_info);
	}

#ifdef  MS_MSPRO_DEBUG
	if(error)
		Debug_Printf("No any valid media type detected!\n");
#endif
	
	return error;
}

int ms_mspro_interface_mode_switching(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_Interface_Mode_t new_interface_mode)
{
	MS_MSPRO_TPC_Packet_t packet;
	//unsigned char sys_para_reg;
	unsigned char* sys_para_reg = ms_mspro_info->data_buf;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	if(disable_port_switch)
		return 0;
	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //READ_REG: Status, Type, Catagory, Class
	packet.param.out.count = 4;
	ms_mspro_buf->ms.reg_set.write_addr = 0x10;          //WRITE_ADRS = 0x10
	ms_mspro_buf->ms.reg_set.write_size = 0x01;          //WRITE_SIZE = 0x01
	packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
	packet.param.out.count = 1;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	if(ms_mspro_info->card_type == CARD_TYPE_MS)
	{                           //Default 0x80: Serial I/F
		//sys_para_reg = (new_interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
		*sys_para_reg = (new_interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	}
	else    //CARD_TYPE_MSPRO
	{                           //Default 0x00: Parallel I/F
		//sys_para_reg = (new_interface_mode==INTERFACE_SERIAL) ? 0x80 : 0x00;
		*sys_para_reg = (new_interface_mode==INTERFACE_SERIAL) ? 0x80 : 0x00;
	}
	//packet.param.out.buffer[0] = sys_para_reg;
	packet.param.out.buffer = sys_para_reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	ms_mspro_info->interface_mode = new_interface_mode;
	
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		unsigned long sdio_config = 0;
		SDIO_Config_Reg_t *config_reg = (void *)&sdio_config;
		sdio_config = READ_CBUS_REG(SDIO_CONFIG);
		if(new_interface_mode == INTERFACE_PARALLEL)
		{
			config_reg->bus_width = 1;
			config_reg->cmd_out_at_posedge = 1;
		}
		else // INTERFACE_SERIAL
		{
			config_reg->bus_width = 0;
			config_reg->cmd_out_at_posedge = 0;
		}
		WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
	}
#endif

	return MS_MSPRO_NO_ERROR;
}

void ms_mspro_endian_convert(Endian_Type_t data_type, void * data)
{
	unsigned temp;
	int i;
	int data_len = data_type;
	unsigned char * data_buf = data;
	
	for(i=0; i<(data_len/2); i++)
	{
		temp = data_buf[i];
		data_buf[i] = data_buf[data_len-i-1];
		data_buf[data_len-i-1] = temp;
	}
}

MS_MSPRO_Media_Type_t ms_mspro_check_media_type(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	if((ms_mspro_buf->ms.regs.Category_Reg >= 0x01) && (ms_mspro_buf->ms.regs.Category_Reg <= 0x7F))
		return MEMORY_STICK_IO;
	
	if(ms_mspro_buf->ms.regs.Type_Reg == 0x01)       // Memory Stick Pro
	{
		if(ms_mspro_buf->ms.regs.Category_Reg == 0x10)
			return MEMORY_STICK_PRO_IO;
		else if(ms_mspro_buf->ms.regs.Category_Reg == 0x00)
		{
			if((ms_mspro_buf->ms.regs.Class_Reg == 0x00) && (ms_mspro_buf->ms.boot_attribute_information.Device_Type == 0))
				return MEMORY_STICK_PRO;
			if(((ms_mspro_buf->ms.regs.Class_Reg == 0x00) || (ms_mspro_buf->ms.regs.Class_Reg == 0x01) || (ms_mspro_buf->ms.regs.Class_Reg == 0x02)) &&
			  (ms_mspro_buf->ms.boot_attribute_information.Device_Type == 1))
				return MEMORY_STICK_PRO_ROM;
			if(((ms_mspro_buf->ms.regs.Class_Reg == 0x00) || (ms_mspro_buf->ms.regs.Class_Reg == 0x02)) &&
			  (ms_mspro_buf->ms.boot_attribute_information.Device_Type == 2))
				return MEMORY_STICK_PRO_R;
		}
	}
	else if((ms_mspro_buf->ms.regs.Type_Reg == 0x00) || (ms_mspro_buf->ms.regs.Type_Reg == 0xFF))     // Memory Stick
	{
		if(ms_mspro_buf->ms.boot_attribute_information.Device_Type == 0)
		{
			if((ms_mspro_buf->ms.regs.Type_Reg == ms_mspro_buf->ms.regs.Category_Reg) && (ms_mspro_buf->ms.regs.Category_Reg == ms_mspro_buf->ms.regs.Class_Reg))
			{
				if(ms_mspro_buf->ms.boot_attribute_information.Parallel_Transfer_Supporting == 0)
					return MEMORY_STICK;
					
				if((ms_mspro_buf->ms.boot_attribute_information.Parallel_Transfer_Supporting == 1) && (ms_mspro_buf->ms.regs.Type_Reg == 0x00))
					return MEMORY_STICK_WITH_SP;
			}
		}
		else if(ms_mspro_buf->ms.boot_attribute_information.Device_Type == 1)
		{
			if(ms_mspro_buf->ms.regs.Type_Reg == ms_mspro_buf->ms.regs.Category_Reg)
			{
				if(ms_mspro_buf->ms.boot_attribute_information.Parallel_Transfer_Supporting == 0)
				{
					if((ms_mspro_buf->ms.regs.Type_Reg == 0x00) &&
					   ((ms_mspro_buf->ms.regs.Class_Reg == 0x00) || (ms_mspro_buf->ms.regs.Class_Reg == 0x01) || (ms_mspro_buf->ms.regs.Class_Reg == 0x02)))
						return MEMORY_STICK_ROM;
					if((ms_mspro_buf->ms.regs.Type_Reg == 0xFF) &&
					   ((ms_mspro_buf->ms.regs.Class_Reg == 0x01) || (ms_mspro_buf->ms.regs.Class_Reg == 0x02) || (ms_mspro_buf->ms.regs.Class_Reg == 0xFF)))
						return MEMORY_STICK_ROM;
				}
				
				if(ms_mspro_buf->ms.boot_attribute_information.Parallel_Transfer_Supporting == 1)
				{
					if((ms_mspro_buf->ms.regs.Type_Reg == 0x00) &&
					   ((ms_mspro_buf->ms.regs.Class_Reg == 0x00) || (ms_mspro_buf->ms.regs.Class_Reg == 0x01) || (ms_mspro_buf->ms.regs.Class_Reg == 0x02)))
						return MEMORY_STICK_ROM_WITH_SP;
				}
			}
		}
		else if(ms_mspro_buf->ms.boot_attribute_information.Device_Type == 2)
		{
			if(ms_mspro_buf->ms.regs.Type_Reg == ms_mspro_buf->ms.regs.Category_Reg)
			{
				if((ms_mspro_buf->ms.boot_attribute_information.Parallel_Transfer_Supporting == 0) && (ms_mspro_buf->ms.regs.Class_Reg == 0x02))
					return MEMORY_STICK_R;
				
				if((ms_mspro_buf->ms.boot_attribute_information.Parallel_Transfer_Supporting == 1) && (ms_mspro_buf->ms.regs.Class_Reg == 0x02))
					return MEMORY_STICK_R_WITH_SP;
			}
		}
	}

	return MEMORY_STICK_ERROR;
}

int ms_mspro_init(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	int error,i;
	int init_retry_flag = 0;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	unsigned long temp;
	
	ms_save_hw_io_config = 0;
	ms_save_hw_io_mult_config = 0;
	ms_save_hw_reg_flag = 0;
	
	if(ms_mspro_info->inited_flag && !ms_mspro_info->removed_flag)
	{
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		ms_sdio_enable(ms_mspro_info->io_pad_type);
#endif		
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		ms_gpio_enable(ms_mspro_info->io_pad_type);
#endif

		error = ms_mspro_cmd_test(ms_mspro_info);
		if(!error)
			return error;
	}
	
	if(++ms_mspro_info->init_retry > MS_MSPRO_INIT_RETRY)
		return MSPRO_ERROR_MEDIA_BREAKDOWN;

#ifdef  MS_MSPRO_DEBUG
	Debug_Printf("\nMS/MSPRO initialization started......\n");
#endif

init_retry:
	
	error = ms_mspro_staff_init(ms_mspro_info);
	if(error)
	{
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_staff_init()!\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}	
	
	error = ms_mspro_media_type_identification(ms_mspro_info);
	if(error)
	{
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_media_type_identification()!\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}
		
	ms_mspro_info->media_type = ms_mspro_check_media_type(ms_mspro_info);

	if(ms_mspro_info->card_type == CARD_TYPE_MS)
	{
		error = ms_search_boot_block(ms_mspro_info, ms_mspro_info->data_buf);
		if(error)
		{
#ifdef  MS_MSPRO_DEBUG
			Debug_Printf("#%s error occured in ms_search_boot_block()!\n", ms_mspro_error_to_string(error));
#endif
			return error;
		}
			
		error = ms_check_boot_block(ms_mspro_info, ms_mspro_info->data_buf);
		if(error)
		{
#ifdef  MS_MSPRO_DEBUG
			Debug_Printf("#%s error occured in ms_check_boot_block()!\n", ms_mspro_error_to_string(error));
#endif
			return error;
		}
		
		error = ms_check_disabled_block(ms_mspro_info, ms_mspro_info->data_buf);
		if(error)
		{
#ifdef  MS_MSPRO_DEBUG
			Debug_Printf("#%s error occured in ms_check_disabled_block()!\n", ms_mspro_error_to_string(error));
#endif
			return error;
		}
		
		//error = ms_read_boot_idi(ms_mspro_buf->ms.page_buf);
		//if(error)
		//{
//#ifdef    MS_MSPRO_DEBUG
		//Debug_Printf("#%s error occured in ms_read_boot_idi()!\n", ms_mspro_error_to_string(error));
//#endif
		//  return error;
		//}

		if(ms_mspro_buf->ms.boot_attribute_information.Parallel_Transfer_Supporting && (!init_retry_flag))
		{
			error = ms_mspro_interface_mode_switching(ms_mspro_info, INTERFACE_PARALLEL);
			if(error)
			{
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_interface_mode_switching()!\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
			
			error = ms_mspro_cmd_test(ms_mspro_info);
			if(error)
			{				
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("This Memory Stick can not work in Parallel I/F mode, retry Serial I/F mode!\n");
#endif
				error = ms_mspro_interface_mode_switching(ms_mspro_info, INTERFACE_SERIAL);
				if(error)
				{					
					ms_mspro_info->interface_mode = INTERFACE_SERIAL;
					error = ms_mspro_cmd_test(ms_mspro_info);
				}
				if(error)
				{
					init_retry_flag = 1;
					goto init_retry;

//#ifdef  MS_MSPRO_DEBUG
//					Debug_Printf("#%s error occured in ms_mspro_interface_mode_switching()!\n", ms_mspro_error_to_string(error));
//#endif
//					return error;

				}
			}
		}
		
		for(i=0; i<MS_MAX_SEGMENT_NUMBERS*MS_BLOCKS_PER_SEGMENT; i++)
			ms_mspro_buf->ms.logical_physical_table[i] = 0xFFFF;
			
		for(i=0; i<MS_MAX_SEGMENT_NUMBERS*MS_MAX_FREE_BLOCKS_PER_SEGMENT; i++)
			ms_mspro_buf->ms.free_block_table[i] = 0xFFFF;
		
		if(ms_mspro_buf->ms.boot_area_protection_process_flag)
		{
			error = ms_boot_area_protection(ms_mspro_info, ms_mspro_info->data_buf);
			if(error)
			{
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_boot_area_protection()!\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
		}
	
		for(i=0; i<ms_mspro_buf->ms.boot_attribute_information.Block_Numbers/MS_BLOCKS_PER_SEGMENT; i++)
		{
			error = ms_logical_physical_table_creation(ms_mspro_info, i);
			if(error)
			{
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_logical_physical_table_creation()!\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
		}
		
		//temp = ms_mspro_buf->ms.boot_attribute_information.Page_Size;
		//ms_mspro_info->blk_len = temp;
		ms_mspro_info->blk_len = MS_PAGE_SIZE;
		temp = ms_mspro_buf->ms.boot_attribute_information.Effective_Block_Numbers - 2;
		temp *= (ms_mspro_buf->ms.boot_attribute_information.Block_Size*2);
		ms_mspro_info->blk_nums = temp;
		//if((ms_mspro_info->blk_nums <= MS_WRITE_ESPECIAL_CAPACITY_BLOCKS)  && ms_power_off_lost_data_flag)
		//	ms_force_write_two_times = 1;
	}
	else if(ms_mspro_info->card_type == CARD_TYPE_MSPRO)
	{
		error = mspro_cpu_startup(ms_mspro_info);
		if(error)
		{
#ifdef  MS_MSPRO_DEBUG
			Debug_Printf("#%s error occured in mspro_cpu_startup()!\n", ms_mspro_error_to_string(error));
#endif
			return error;
		}
			
		error = mspro_confirm_attribute_information(ms_mspro_info, ms_mspro_info->data_buf);
		if(error)
		{
#ifdef  MS_MSPRO_DEBUG
			Debug_Printf("#%s error occured in mspro_confirm_attribute_information()!\n", ms_mspro_error_to_string(error));
#endif
			return error;
		}
			
		error = mspro_confirm_system_information(ms_mspro_info, ms_mspro_info->data_buf);
		if(error)
		{
#ifdef  MS_MSPRO_DEBUG
			Debug_Printf("#%s error occured in mspro_confirm_system_information()!\n", ms_mspro_error_to_string(error));
#endif
			return error;
		}
		
		if(ms_mspro_buf->mspro.system_information.Interface_Type == 1 && (!init_retry_flag))
		{
			error = ms_mspro_interface_mode_switching(ms_mspro_info, INTERFACE_PARALLEL);
			if(error)
			{
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_interface_mode_switching()!\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
			
			error = ms_mspro_cmd_test(ms_mspro_info);
			if(error)
			{
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("This Memory Stick Pro can not work in Parallel I/F mode, retry Serial I/F mode!\n");
#endif
				error = ms_mspro_interface_mode_switching(ms_mspro_info, INTERFACE_SERIAL);
				if(error)
				{
					ms_mspro_info->interface_mode = INTERFACE_SERIAL;
					error = ms_mspro_cmd_test(ms_mspro_info);
				}
				if(error)
				{
					init_retry_flag =1;
					goto init_retry;

//#ifdef  MS_MSPRO_DEBUG
//					Debug_Printf("#%s error occured in ms_mspro_interface_mode_switching()!\n", ms_mspro_error_to_string(error));
//#endif
//					return error;
				}
			}
		}
		
		//temp = ms_mspro_buf->mspro.system_information.Page_Size;
		//temp *= ms_mspro_buf->mspro.system_information.Unit_Size;
		//ms_mspro_info->blk_len = temp;
		ms_mspro_info->blk_len = MSPRO_SECTOR_SIZE;
		temp = ms_mspro_buf->mspro.system_information.User_Area_Blocks;
		temp *= ms_mspro_buf->mspro.system_information.Block_Size;
		ms_mspro_info->blk_nums = temp;
	}
	else
	{
		error = MS_MSPRO_ERROR_MEDIA_TYPE;
		
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in mspro_confirm_system_information()!\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}

	//error = ms_mspro_check_data_consistency();
	//if(error)
	//{
//#ifdef  MS_MSPRO_DEBUG
//		Debug_Printf("#%s error occured in ms_mspro_check_data_consistency()!\n", ms_mspro_error_to_string(error));
//#endif
//		return error;
//	}
	
#ifdef  MS_MSPRO_DEBUG
	Debug_Printf("ms_mspro_init() is completed successfully!\n");
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		if(ms_mspro_info->card_type == CARD_TYPE_MS)
			Debug_Printf("This Memory Stick card is working in Parallel I/F mode!\n\n");
		else
			Debug_Printf("This Memory Stick Pro card is working in Parallel I/F mode!\n\n");
	}
	else
	{
		if(ms_mspro_info->card_type == CARD_TYPE_MS)
			Debug_Printf("This Memory Stick card is working in Serial I/F mode!\n\n");
		else
			Debug_Printf("This Memory Stick Pro card is working in Serial I/F mode!\n\n");
	}
#endif

	ms_mspro_info->inited_flag = 1;
	ms_mspro_info->init_retry = 0;
	ms_mspro_info->raw_cid = ms_mspro_info->card_type;

    ms_save_hw_io_config = READ_CBUS_REG(SDIO_CONFIG);
    ms_save_hw_io_mult_config = READ_CBUS_REG(SDIO_MULT_CONFIG);
	ms_save_hw_reg_flag = 1;

	return MS_MSPRO_NO_ERROR;
}

int ms_mspro_check_insert(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	int level;
	
	if(ms_mspro_info->ms_mspro_get_ins)
	{
		level = ms_mspro_info->ms_mspro_get_ins();
	}
	else
	{
		ms_set_ins_input()
		level = ms_get_ins_value();
	}
	if(level)
	{
		if(ms_mspro_info->init_retry)
		{
			ms_mspro_power_off(ms_mspro_info);
			ms_mspro_info->init_retry = 0;
		}
			
		if(ms_mspro_info->inited_flag)
		{
			ms_mspro_power_off(ms_mspro_info);
			ms_mspro_info->removed_flag = 1;
			ms_mspro_info->inited_flag = 0;
		}
			
		return 0;
	}
	else
	{
		return 1;
	}
}

#ifdef MS_MSPRO_HW_CONTROL
int ms_mspro_read_data_hw(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error,tracer,i;
	
	unsigned long sector_nums, data_offset;
	unsigned short logical_no,physical_no,page_no,last_seg_no,last_logical_no,page_nums;
	unsigned long pages_per_block;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);

	MS_MSPRO_TPC_Packet_t packet;
	unsigned char* buf = ms_mspro_info->data_buf;
	
	if(byte_cnt == 0)
	{
		error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_read_data()\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}

	 ms_sdio_enable(ms_mspro_info->io_pad_type);
	 
	if(ms_mspro_info->card_type == CARD_TYPE_MSPRO)
	{
		sector_nums = (byte_cnt+MSPRO_SECTOR_SIZE-1)/MSPRO_SECTOR_SIZE;
		error = mspro_read_user_sector(ms_mspro_info, lba, sector_nums, data_buf);
		tracer=0;
		while (1)
		{
			if (error == 0)
				break;
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
			packet.param.out.count = 1;
			packet.param.out.buffer = buf;
			packet.param.out.buffer[0] = CMD_MSPRO_STOP;
			if (ms_mspro_packet_communicate(ms_mspro_info, &packet))
			{
				if (ms_mspro_packet_communicate(ms_mspro_info, &packet))
				{
					if (ms_mspro_packet_communicate(ms_mspro_info, &packet))
		{
						ms_mspro_packet_communicate(ms_mspro_info, &packet);
					}
				}
			}
			if (mass_counter == 0)
				tracer++;
			else
				tracer=0;
			if (tracer>3)
				break;
			lba += mass_counter;
			sector_nums -= mass_counter;
			data_buf += MSPRO_SECTOR_SIZE*mass_counter;
			error = mspro_read_user_sector(ms_mspro_info, lba, sector_nums, data_buf);
		}	
			if(error)
			{
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in mspro_read_user_sector()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
	}
	else
	{
		data_offset = 0;
		page_nums = (byte_cnt+MS_PAGE_SIZE-1)/MS_PAGE_SIZE;
		last_seg_no = ms_mspro_buf->ms.boot_attribute_information.Block_Numbers;
		last_seg_no = last_seg_no / MS_BLOCKS_PER_SEGMENT - 1;
		last_logical_no = (last_seg_no+1)*MS_LOGICAL_SIZE_PER_SEGMENT-2-1;
		pages_per_block = ms_mspro_buf->ms.boot_attribute_information.Block_Size;
		pages_per_block *= 2;
		
		while(page_nums)
		{
			logical_no = lba / pages_per_block;
			if(logical_no > last_logical_no)
			{
				error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_read_data()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
		
			physical_no = ms_mspro_buf->ms.logical_physical_table[logical_no];
			if(physical_no >= ms_mspro_buf->ms.boot_attribute_information.Block_Numbers)
			{
				error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_read_data()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
		
			page_no = lba - logical_no*pages_per_block;
		
			if(page_nums > (pages_per_block - page_no))
			{
				for(i=0; i<(pages_per_block - page_no); i++)
				{
					error = ms_read_page(ms_mspro_info, physical_no, page_no++, data_buf);
					if(error)
					{
#ifdef  MS_MSPRO_DEBUG
						Debug_Printf("#%s error occured in ms_read_block()\n", ms_mspro_error_to_string(error));
#endif
						return error;
					}
				
#ifdef AMLOGIC_CHIP_SUPPORT
					data_buf += ((unsigned long)data_buf == 0x3400000) ? 0 : MS_PAGE_SIZE;
#else
					data_buf += MS_PAGE_SIZE;
#endif
					lba++;
					page_nums--;
				}
			}
			else
			{
				for(i=0; i<page_nums; i++)
				{
					error = ms_read_page(ms_mspro_info, physical_no, page_no++, data_buf);
					if(error)
					{
#ifdef  MS_MSPRO_DEBUG
						Debug_Printf("#%s error occured in ms_read_block()\n", ms_mspro_error_to_string(error));
#endif
						return error;
					}
				
#ifdef AMLOGIC_CHIP_SUPPORT
					data_buf += ((unsigned long)data_buf == 0x3400000) ? 0 : MS_PAGE_SIZE;
#else
					data_buf += MS_PAGE_SIZE;
#endif
					lba++;
					page_nums--;
				}
			}
		}
	}

	return MS_MSPRO_NO_ERROR;
}
#endif

#ifdef MS_MSPRO_SW_CONTROL
//#define MS_ONESECTOR_PERREAD


int ms_mspro_read_data_sw(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error;
    int retry = 0;
	unsigned long sector_nums, data_offset;
	unsigned short logical_no,physical_no,page_no,last_seg_no,last_logical_no,page_nums;
	unsigned long pages_per_block;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	if(byte_cnt == 0)
	{
		error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_read_data()\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}
	
	ms_mspro_io_config(ms_mspro_info);
	
	if(ms_mspro_info->card_type == CARD_TYPE_MSPRO)
    {
        sector_nums = (byte_cnt+MSPRO_SECTOR_SIZE-1)/MSPRO_SECTOR_SIZE;
#ifdef MS_ONESECTOR_PERREAD
        int i;
        for(i=0;i<sector_nums;i++){
            do {
                error = mspro_read_user_sector(ms_mspro_info, lba+i, 1, data_buf+(MSPRO_SECTOR_SIZE*i));
            } while(error && retry++<16);
            if(error)
                break;
        }    
#else
        do {
            error = mspro_read_user_sector(ms_mspro_info, lba, sector_nums, data_buf);
        } while(error && retry++<16);
#endif
        if(error)
        {
#ifdef  MS_MSPRO_DEBUG
            Debug_Printf("#%s error occured in mspro_read_user_sector()\n", ms_mspro_error_to_string(error));
#endif
            return error;
		}
	}
	else
	{
		data_offset = 0;
		page_nums = (byte_cnt+MS_PAGE_SIZE-1)/MS_PAGE_SIZE;
		last_seg_no = ms_mspro_buf->ms.boot_attribute_information.Block_Numbers;
		last_seg_no = last_seg_no / MS_BLOCKS_PER_SEGMENT - 1;
		last_logical_no = (last_seg_no+1)*MS_LOGICAL_SIZE_PER_SEGMENT-2-1;
		pages_per_block = ms_mspro_buf->ms.boot_attribute_information.Block_Size;
		pages_per_block *= 2;
		
		while(page_nums)
		{
			logical_no = lba / pages_per_block;
			if(logical_no > last_logical_no)
			{
				error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_read_data()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
		
			physical_no = ms_mspro_buf->ms.logical_physical_table[logical_no];
			if(physical_no >= ms_mspro_buf->ms.boot_attribute_information.Block_Numbers)
			{
				error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_read_data()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
		
			page_no = lba - logical_no*pages_per_block;
		
			if(page_nums > (pages_per_block - page_no))
			{
				error = ms_read_block(ms_mspro_info, physical_no, page_no, (pages_per_block - page_no), data_buf+data_offset);
				if(error)
				{
#ifdef  MS_MSPRO_DEBUG
					Debug_Printf("#%s error occured in ms_read_block()\n", ms_mspro_error_to_string(error));
#endif
					return error;
				}
				
#ifdef AMLOGIC_CHIP_SUPPORT
				data_offset += ((unsigned long)data_buf == 0x3400000) ? 0 : (pages_per_block - page_no)*MS_PAGE_SIZE;
#else
				data_offset += (pages_per_block - page_no)*MS_PAGE_SIZE;
#endif
				lba += (pages_per_block - page_no);
				page_nums -= (pages_per_block - page_no);
			}
			else
			{
				error = ms_read_block(ms_mspro_info, physical_no, page_no, page_nums, data_buf+data_offset);
				if(error)
				{
#ifdef  MS_MSPRO_DEBUG
					Debug_Printf("#%s error occured in ms_read_block()\n", ms_mspro_error_to_string(error));
#endif
					return error;
				}
				
#ifdef AMLOGIC_CHIP_SUPPORT
				data_offset += ((unsigned long)data_buf == 0x3400000) ? 0 : page_nums*MS_PAGE_SIZE;
#else
				data_offset += page_nums*MS_PAGE_SIZE;
#endif
				lba += page_nums;
				page_nums -= page_nums;
			}
		}
	}

	return MS_MSPRO_NO_ERROR;
}
#endif

#ifdef MS_MSPRO_HW_CONTROL
int ms_mspro_write_data_hw(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error,tracer,i,j,k;
	
	unsigned long sector_nums, data_offset;
	unsigned short logical_no,physical_no,page_no,seg_no,last_seg_no,last_logical_no,page_nums,free_blk_no;
	unsigned long pages_per_block,free_table_index;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	MS_MSPRO_TPC_Packet_t packet;
	unsigned char* buf = ms_mspro_info->data_buf;
	
	if(byte_cnt == 0)
	{
		error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}
	
	if(ms_mspro_info->write_protected_flag)
	{
		error = MS_MSPRO_ERROR_WRITE_PROTECTED;
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}
	
	if(ms_mspro_info->read_only_flag)
	{
		error = MS_MSPRO_ERROR_READ_ONLY;
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}
    
     ms_sdio_enable(ms_mspro_info->io_pad_type);
     
	if(ms_mspro_info->card_type == CARD_TYPE_MSPRO)
	{
		sector_nums = (byte_cnt+MSPRO_SECTOR_SIZE-1)/MSPRO_SECTOR_SIZE;
		error = mspro_write_user_sector(ms_mspro_info, lba, sector_nums, data_buf);
		tracer=0;
		while (1)
		{
			if (error == 0)
				break;
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
			packet.param.out.count = 1;
			packet.param.out.buffer = buf;
			packet.param.out.buffer[0] = CMD_MSPRO_STOP;
			if (ms_mspro_packet_communicate(ms_mspro_info, &packet))
			{
				if (ms_mspro_packet_communicate(ms_mspro_info, &packet))
				{
					if (ms_mspro_packet_communicate(ms_mspro_info, &packet))
		{
						ms_mspro_packet_communicate(ms_mspro_info, &packet);
					}
				}
			}
			if (mass_counter == 0)
				tracer++;
			else
				tracer=0;
			if (tracer>3)
				break;
			lba += mass_counter;
			sector_nums -= mass_counter;
			data_buf += MSPRO_SECTOR_SIZE*mass_counter;
			error = mspro_write_user_sector(ms_mspro_info, lba, sector_nums, data_buf);
		}	
			if(error)
			{
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in mspro_write_user_sector()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
	}
	else
	{
		data_offset = 0;
		page_nums = (byte_cnt+MS_PAGE_SIZE-1)/MS_PAGE_SIZE;
		last_seg_no = ms_mspro_buf->ms.boot_attribute_information.Block_Numbers;
		last_seg_no = last_seg_no / MS_BLOCKS_PER_SEGMENT - 1;
		last_logical_no = (last_seg_no+1)*MS_LOGICAL_SIZE_PER_SEGMENT-2-1;
		pages_per_block = ms_mspro_buf->ms.boot_attribute_information.Block_Size;
		pages_per_block *= 2;
		
		while(page_nums)
		{
			logical_no = lba / pages_per_block;
			if(logical_no > last_logical_no)
			{
				error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
		
			physical_no = ms_mspro_buf->ms.logical_physical_table[logical_no];
			if(physical_no >= ms_mspro_buf->ms.boot_attribute_information.Block_Numbers)
			{
				error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
		
			page_no = lba - logical_no*pages_per_block;
			
			seg_no = physical_no / MS_BLOCKS_PER_SEGMENT;
			
			for(i=0; i<MS_MAX_SEGMENT_NUMBERS; i++)
			{
				free_table_index = seg_no*MS_MAX_FREE_BLOCKS_PER_SEGMENT+i;
				free_blk_no = ms_mspro_buf->ms.free_block_table[free_table_index];
				if(free_blk_no != 0xFFFF)
				{
					error = ms_read_extra_data(ms_mspro_info, free_blk_no, 0);
					if(error)
						continue;
						
					if(((MS_Overwrite_Flag_Register_t *)&ms_mspro_buf->ms.regs.Overwrite_Flag_Reg)->UDST)
						break;
				}
			}
			if(i >= MS_MAX_SEGMENT_NUMBERS)
			{
				error = MS_ERROR_NO_FREE_BLOCK;
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}

			error = ms_erase_block(ms_mspro_info, free_blk_no);
			if(error)
			{
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_erase_block()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
				
			for(j=0; j<page_no; j++)
			{
				error = ms_copy_page(ms_mspro_info, physical_no, j, free_blk_no, j, ms_mspro_info->data_buf);
				if(error)
				{
#ifdef  MS_MSPRO_DEBUG
					Debug_Printf("#%s error occured in ms_copy_page()\n", ms_mspro_error_to_string(error));
#endif
					return error;
				}
			}

			ms_mspro_buf->ms.regs.Logical_Address_Reg1 = (logical_no>>8) & 0xFF;
			ms_mspro_buf->ms.regs.Logical_Address_Reg0 = logical_no & 0xFF;
			
			if(page_nums > (pages_per_block - page_no))
			{
				for(i=page_no; i<pages_per_block; i++)
				{
				  error = ms_write_page(ms_mspro_info, free_blk_no, i, data_buf+data_offset);
				  if(error)
				  {
#ifdef    MS_MSPRO_DEBUG
				      Debug_Printf("#%s error occured in ms_write_page()\n", ms_mspro_error_to_string(error));
#endif
				      return error;
				  }
				
				  data_offset += MS_PAGE_SIZE;
				}
				
//				error = ms_write_block(free_blk_no, page_no, (pages_per_block - page_no), data_buf+data_offset);
//				if(error)
//				{
//#ifdef  MS_MSPRO_DEBUG
//					Debug_Printf("#%s error occured in ms_write_block()\n", ms_mspro_error_to_string(error));
//#endif
//					return error;
//				}
				
				//data_offset += (pages_per_block - page_no)*MS_PAGE_SIZE;
				
				lba += (pages_per_block - page_no);
				page_nums -= (pages_per_block - page_no);
			}
			else
			{
				for(i=page_no; i<(page_no+page_nums); i++)
				{
				  error = ms_write_page(ms_mspro_info, free_blk_no, i, data_buf+data_offset);
				  if(error)
				  {
#ifdef    MS_MSPRO_DEBUG
				      Debug_Printf("#%s error occured in ms_write_page()\n", ms_mspro_error_to_string(error));
#endif
				      return error;
				  }
				
				  data_offset += MS_PAGE_SIZE;
				}
				
//				error = ms_write_block(free_blk_no, page_no, page_nums, data_buf+data_offset);
//				if(error)
//				{
//#ifdef  MS_MSPRO_DEBUG
//					Debug_Printf("#%s error occured in ms_write_block()\n", ms_mspro_error_to_string(error));
//#endif
//					return error;
//				}
				
				//data_offset += page_nums*MS_PAGE_SIZE;

				for(k=(page_no+page_nums); k<pages_per_block; k++)
				{
					error = ms_copy_page(ms_mspro_info, physical_no, k, free_blk_no, k, ms_mspro_info->data_buf);
					if(error)
					{
#ifdef  MS_MSPRO_DEBUG
						Debug_Printf("#%s error occured in ms_copy_page()\n", ms_mspro_error_to_string(error));
#endif
						return error;
					}
				}
				
				lba += page_nums;
				page_nums -= page_nums;
			}
			
			ms_mspro_buf->ms.free_block_table[free_table_index] = physical_no;
			ms_mspro_buf->ms.logical_physical_table[logical_no] = free_blk_no;
			
			//error = ms_erase_block(physical_no);
			//if(error)
			//{
//#ifdef    MS_MSPRO_DEBUG
			//  Debug_Printf("#%s error occured in ms_erase_block()\n", ms_mspro_error_to_string(error));
//#endif
			//  return error;
			//}
		}
	}

	return MS_MSPRO_NO_ERROR;
}
#endif

#ifdef MS_MSPRO_SW_CONTROL
int ms_mspro_write_data_sw(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error,i,j,k;
	
	unsigned long sector_nums, data_offset;
	unsigned short logical_no,physical_no,page_no,seg_no,last_seg_no,last_logical_no,page_nums,free_blk_no;
	unsigned long pages_per_block,free_table_index;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	if(byte_cnt == 0)
	{
		error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}
	
	if(ms_mspro_info->write_protected_flag)
	{
		error = MS_MSPRO_ERROR_WRITE_PROTECTED;
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}
	
	if(ms_mspro_info->read_only_flag)
	{
		error = MS_MSPRO_ERROR_READ_ONLY;
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
		return error;
	}

	ms_mspro_io_config(ms_mspro_info);

	if(ms_mspro_info->card_type == CARD_TYPE_MSPRO)
	{
		sector_nums = (byte_cnt+MSPRO_SECTOR_SIZE-1)/MSPRO_SECTOR_SIZE;
		
		//error = mspro_erase_user_sector(lba, sector_nums);
		//if(error)
		//  return error;
			
		error = mspro_write_user_sector(ms_mspro_info, lba, sector_nums, data_buf);
		if(error)
		{
#ifdef  MS_MSPRO_DEBUG
			Debug_Printf("#%s error occured in mspro_write_user_sector()\n", ms_mspro_error_to_string(error));
#endif
			return error;
		}
	}
	else
	{
		data_offset = 0;
		page_nums = (byte_cnt+MS_PAGE_SIZE-1)/MS_PAGE_SIZE;
		last_seg_no = ms_mspro_buf->ms.boot_attribute_information.Block_Numbers;
		last_seg_no = last_seg_no / MS_BLOCKS_PER_SEGMENT - 1;
		last_logical_no = (last_seg_no+1)*MS_LOGICAL_SIZE_PER_SEGMENT-2-1;
		pages_per_block = ms_mspro_buf->ms.boot_attribute_information.Block_Size;
		pages_per_block *= 2;
		
		while(page_nums)
		{
			logical_no = lba / pages_per_block;
			if(logical_no > last_logical_no)
			{
				error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
		
			physical_no = ms_mspro_buf->ms.logical_physical_table[logical_no];
			if(physical_no >= ms_mspro_buf->ms.boot_attribute_information.Block_Numbers)
			{
				error = MS_MSPRO_ERROR_PARAMETER;
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
		
			page_no = lba - logical_no*pages_per_block;
			
			seg_no = physical_no / MS_BLOCKS_PER_SEGMENT;
			
			for(i=0; i<MS_MAX_SEGMENT_NUMBERS; i++)
			{
				free_table_index = seg_no*MS_MAX_FREE_BLOCKS_PER_SEGMENT+i;
				free_blk_no = ms_mspro_buf->ms.free_block_table[free_table_index];
				if(free_blk_no != 0xFFFF)
				{
					error = ms_read_extra_data(ms_mspro_info, free_blk_no, 0);
					if(error)
						continue;
						
					if(((MS_Overwrite_Flag_Register_t *)&ms_mspro_buf->ms.regs.Overwrite_Flag_Reg)->UDST)
						break;
				}
			}
			if(i >= MS_MAX_SEGMENT_NUMBERS)
			{
				error = MS_ERROR_NO_FREE_BLOCK;
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_mspro_write_data()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}

			error = ms_erase_block(ms_mspro_info, free_blk_no);
			if(error)
			{
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("#%s error occured in ms_erase_block()\n", ms_mspro_error_to_string(error));
#endif
				return error;
			}
				
			for(j=0; j<page_no; j++)
			{
				error = ms_copy_page(ms_mspro_info, physical_no, j, free_blk_no, j, ms_mspro_info->data_buf);
				if(error)
				{
#ifdef  MS_MSPRO_DEBUG
					Debug_Printf("#%s error occured in ms_copy_page()\n", ms_mspro_error_to_string(error));
#endif
					return error;
				}
			}

			ms_mspro_buf->ms.regs.Logical_Address_Reg1 = (logical_no>>8) & 0xFF;
			ms_mspro_buf->ms.regs.Logical_Address_Reg0 = logical_no & 0xFF;
			
			if(page_nums > (pages_per_block - page_no))
			{
				//for(int i=page_no; i<pages_per_block; i++)
				//{
				//  error = ms_write_page(free_blk_no, i, data_buf+data_offset);
				//  if(error)
				//  {
//#ifdef    MS_MSPRO_DEBUG
				//      Debug_Printf("#%s error occured in ms_write_page()\n", ms_mspro_error_to_string(error));
//#endif
				//      return error;
				//  }
				//
				//  data_offset += MS_PAGE_SIZE;
				//}
				
				error = ms_write_block(ms_mspro_info, free_blk_no, page_no, (pages_per_block - page_no), data_buf+data_offset);
				if(error)
				{
#ifdef  MS_MSPRO_DEBUG
					Debug_Printf("#%s error occured in ms_write_block()\n", ms_mspro_error_to_string(error));
#endif
					return error;
				}
				
				data_offset += (pages_per_block - page_no)*MS_PAGE_SIZE;
				
				lba += (pages_per_block - page_no);
				page_nums -= (pages_per_block - page_no);
			}
			else
			{
				//for(int i=page_no; i<(page_no+page_nums); i++)
				//{
				//  error = ms_write_page(free_blk_no, i, data_buf+data_offset);
				//  if(error)
				//  {
//#ifdef    MS_MSPRO_DEBUG
				//      Debug_Printf("#%s error occured in ms_write_page()\n", ms_mspro_error_to_string(error));
//#endif
				//      return error;
				//  }
				//
				//  data_offset += MS_PAGE_SIZE;
				//}
				
				error = ms_write_block(ms_mspro_info, free_blk_no, page_no, page_nums, data_buf+data_offset);
				if(error)
				{
#ifdef  MS_MSPRO_DEBUG
					Debug_Printf("#%s error occured in ms_write_block()\n", ms_mspro_error_to_string(error));
#endif
					return error;
				}
				
				data_offset += page_nums*MS_PAGE_SIZE;

				for(k=(page_no+page_nums); k<pages_per_block; k++)
				{
					error = ms_copy_page(ms_mspro_info, physical_no, k, free_blk_no, k, ms_mspro_info->data_buf);
					if(error)
					{
#ifdef  MS_MSPRO_DEBUG
						Debug_Printf("#%s error occured in ms_copy_page()\n", ms_mspro_error_to_string(error));
#endif
						return error;
					}
				}
				
				lba += page_nums;
				page_nums -= page_nums;
			}
			
			ms_mspro_buf->ms.free_block_table[free_table_index] = physical_no;
			ms_mspro_buf->ms.logical_physical_table[logical_no] = free_blk_no;
			
			//error = ms_erase_block(physical_no);
			//if(error)
			//{
//#ifdef    MS_MSPRO_DEBUG
			//  Debug_Printf("#%s error occured in ms_erase_block()\n", ms_mspro_error_to_string(error));
//#endif
			//  return error;
			//}
		}
	}

	return MS_MSPRO_NO_ERROR;
}
#endif

void ms_mspro_io_config(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	int i;
	ms_gpio_enable(ms_mspro_info->io_pad_type);
	
	ms_set_clk_output();
	ms_set_clk_high();
	ms_set_dat0_3_input();
	ms_set_bs_output();
	ms_set_bs_state(0);

	for(i=0; i<10; i++)
	{
		ms_clk_serial_low();
		ms_clk_serial_high();
	}
}

int ms_mspro_staff_init(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	unsigned long sdio_config;
	unsigned long sdio_multi_config;
	SDIO_Multi_Config_Reg_t *multi_config_reg;
	SDIO_Config_Reg_t *config_reg;
	
	ms_mspro_prepare_power(ms_mspro_info);
	ms_mspro_power_on(ms_mspro_info);

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		sdio_multi_config = (READ_CBUS_REG(SDIO_MULT_CONFIG) & 0x00000003);
		multi_config_reg = (void *)&sdio_multi_config;
		multi_config_reg->ms_enable = 1;
		multi_config_reg->ms_sclk_always = 1;
		WRITE_CBUS_REG(SDIO_MULT_CONFIG, sdio_multi_config);
	
		sdio_config = 0;
		config_reg = (void *)&sdio_config;
		config_reg->cmd_clk_divide = 3;//aml_system_clk / (2*MS_MSPRO_TRANSFER_SLOWER_CLK) -1;
		config_reg->m_endian = 3;
		WRITE_CBUS_REG(SDIO_CONFIG, sdio_config);
		ms_mspro_info->ms_clk_unit = 1000/MS_MSPRO_TRANSFER_CLK;
		
		ms_sdio_enable(ms_mspro_info->io_pad_type);
	}
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		ms_mspro_io_config(ms_mspro_info);
#endif

	ms_mspro_info->card_type = CARD_NONE_TYPE;
	ms_mspro_info->media_type = MEMORY_STICK_ERROR;
	ms_mspro_info->interface_mode = INTERFACE_SERIAL;
		
	ms_mspro_info->blk_len = 0;
	ms_mspro_info->blk_nums = 0;
	
	ms_mspro_info->write_protected_flag = 0;
	ms_mspro_info->read_only_flag = 0;
	
	ms_mspro_info->inited_flag = 0;
	ms_mspro_info->removed_flag = 0;
	
	return MS_MSPRO_NO_ERROR;
}

int ms_mspro_cmd_test(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	MS_MSPRO_TPC_Packet_t packet;
	int error;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;
	packet.param.out.count = 4;             				//READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	ms_mspro_buf->mspro.reg_set.write_addr = 0x11;
	ms_mspro_buf->mspro.reg_set.write_size = 0x06;
	packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->mspro.reg_set;
	
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	
	return error;
}

void ms_mspro_power_on(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	ms_delay_ms(ms_mspro_power_delay+1);


#ifdef MS_MSPRO_POWER_CONTROL
	if(ms_mspro_info->ms_mspro_power)
	{
		ms_mspro_info->ms_mspro_power(0);
	}
	else	
	{
		ms_set_disable();
	}
	ms_delay_ms(200);
	if(ms_mspro_info->ms_mspro_power)
	{
		if(ms_mspro_check_insert(ms_mspro_info)) //ensure card wasn't removed at this time
		{
			ms_mspro_info->ms_mspro_power(1);
		}
	}
	else
	{
		if(ms_mspro_check_insert(ms_mspro_info)) //ensure card wasn't removed at this time
		{
			ms_set_enable();
		}
	}
	ms_delay_ms(200);
#else
	ms_delay_ms(200);
#endif

}

void ms_mspro_power_off(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
#ifdef MS_MSPRO_POWER_CONTROL
	if(ms_mspro_info->ms_mspro_power)
	{
		ms_mspro_info->ms_mspro_power(0);
	}
	else
	{
		ms_set_disable();
	}
#endif
}

//check data lines consistency
int ms_mspro_check_data_consistency(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	int error;
	unsigned char *mbr_buf = ms_mspro_info->data_buf;
	
	//This card is working in parallel bus mode!
	memset(mbr_buf, 0, ms_mspro_info->blk_len*2);
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		//read MBR information
		error = ms_mspro_read_data(ms_mspro_info, 0, ms_mspro_info->blk_len*2, mbr_buf);
		if(error)
		{
			//error! retry again!
			error = ms_mspro_read_data(ms_mspro_info, 0, ms_mspro_info->blk_len, mbr_buf);
			if(error)
				return error;
		}

		error = ms_mspro_read_data(ms_mspro_info, 0, ms_mspro_info->blk_len*2, mbr_buf);
		if(error)
		{
			//error! retry again!
			error = ms_mspro_read_data(ms_mspro_info, 0, ms_mspro_info->blk_len, mbr_buf);
			if(error)
				return error;
		}

		//check MBR data consistency
		if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
		{
			//data consistency error! retry again!
			error = ms_mspro_read_data(ms_mspro_info, 0, ms_mspro_info->blk_len, mbr_buf);
			if(error)
				return error;
			
			//check MBR data consistency
			if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
			{
#ifdef  MS_MSPRO_DEBUG
				Debug_Printf("MS/MSPRO data consistency error in Parallel I/F mode! Try Serial I/F mode...\n");
#endif

				//error again! retry serial bus mode!
				error = ms_mspro_interface_mode_switching(ms_mspro_info, INTERFACE_SERIAL);
				if(error)
					return error;
			}
			else
			{
				return MS_MSPRO_NO_ERROR;
			}
		}
		else
		{
			return MS_MSPRO_NO_ERROR;
		}
	}

	//This card is working in serial bus mode!
	memset(mbr_buf, 0, ms_mspro_info->blk_len);
	//read MBR information
	error = ms_mspro_read_data(ms_mspro_info, 0, ms_mspro_info->blk_len, mbr_buf);
	if(error)
	{
		//error! retry again!
		error = ms_mspro_read_data(ms_mspro_info, 0, ms_mspro_info->blk_len, mbr_buf);
		if(error)
			return error;
	}
		
	//check MBR data consistency
	if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
	{
		//data consistency error! retry again!
		error = ms_mspro_read_data(ms_mspro_info, 0, ms_mspro_info->blk_len, mbr_buf);
		if(error)
			return error;
			
		//check MBR data consistency
		if((mbr_buf[510] != 0x55) || (mbr_buf[511] != 0xAA))
		{
			return MS_MSPRO_ERROR_DATA_CRC;
		}
	}
	
	return MS_MSPRO_NO_ERROR;
}

int ms_mspro_packet_communicate(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet)
{
	int error = 0;
	
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		error = ms_mspro_packet_communicate_hw(ms_mspro_info, tpc_packet);
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		error = ms_mspro_packet_communicate_sw(ms_mspro_info, tpc_packet);
#endif

		return error;
}

int ms_mspro_read_data(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error = 0;
	
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
    {
    	if(ms_save_hw_reg_flag)
    	{
        	WRITE_CBUS_REG(SDIO_CONFIG, ms_save_hw_io_config);
        	WRITE_CBUS_REG(SDIO_MULT_CONFIG, ms_save_hw_io_mult_config);
        }
		error = ms_mspro_read_data_hw(ms_mspro_info, lba, byte_cnt, data_buf);	
	}
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		error = ms_mspro_read_data_sw(ms_mspro_info, lba, byte_cnt, data_buf);
#endif

	return error;
}

int ms_mspro_wait_int(MS_MSPRO_Card_Info_t *ms_mspro_info, MS_MSPRO_TPC_Packet_t * tpc_packet)
{
	int error = 0;
	
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		if(ms_save_hw_reg_flag)
    	{
        	WRITE_CBUS_REG(SDIO_CONFIG, ms_save_hw_io_config);
        	WRITE_CBUS_REG(SDIO_MULT_CONFIG, ms_save_hw_io_mult_config);
        }
		error = ms_mspro_wait_int_hw(ms_mspro_info, tpc_packet);
	}
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		error = ms_mspro_wait_int_sw(ms_mspro_info, tpc_packet);
#endif

	return error;
}

int ms_mspro_write_data(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long lba, unsigned long byte_cnt, unsigned char * data_buf)
{
	int error = 0;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
    	if(ms_save_hw_reg_flag)
    	{
        	WRITE_CBUS_REG(SDIO_CONFIG, ms_save_hw_io_config);
        	WRITE_CBUS_REG(SDIO_MULT_CONFIG, ms_save_hw_io_mult_config);
        }
		error = ms_mspro_write_data_hw(ms_mspro_info, lba, byte_cnt, data_buf);
	}
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{	
	#ifdef AVOS		
		ms_delay_ms(2);	
	#endif 	
		error = ms_mspro_write_data_sw(ms_mspro_info, lba, byte_cnt, data_buf);			
	}		
#endif

	return error;
}

void ms_mspro_exit(MS_MSPRO_Card_Info_t *ms_mspro_info)
{	
	if(ms_mspro_info->ms_mspro_io_release)
		ms_mspro_info->ms_mspro_io_release();
		
	check_one_boot_block = 0;
	mspro_access_status_reg_after_read = 0;
}

void ms_mspro_prepare_init(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	if(ms_mspro_power_register)
		ms_mspro_info->ms_mspro_power = ms_mspro_power_register;
	if(ms_mspro_ins_register)
		ms_mspro_info->ms_mspro_get_ins = ms_mspro_ins_register;
	if(ms_mspro_io_release_register)
		ms_mspro_info->ms_mspro_io_release = ms_mspro_io_release_register;
}

void ms_mspro_prepare_power(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	ms_gpio_enable(ms_mspro_info->io_pad_type);
	
	ms_set_clk_output();
	ms_set_clk_low();
	ms_set_dat0_3_output();
	ms_set_dat0_3_value(0);
	ms_set_bs_output();
	ms_set_bs_state(0);
}

