#include "ms_port.h"
#include "ms_misc.h"
#include "ms_mspro.h"
#include "mspro_protocol.h"

unsigned char read_sector_type;
extern unsigned short mass_counter;
extern unsigned mspro_access_status_reg_after_read;

int mspro_media_type_identification(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);

	if (ms_mspro_buf->mspro.regs.Type_Reg != 0x01)
		return MS_MSPRO_ERROR_MEDIA_TYPE;
		
	if (ms_mspro_buf->mspro.regs.Category_Reg != 0x00)
		return MS_MSPRO_ERROR_MEDIA_TYPE;
		
	if (ms_mspro_buf->mspro.regs.Class_Reg == 0x00)
	{
		ms_mspro_info->write_protected_flag = ((MSPRO_Status_Register_t*)&ms_mspro_buf->mspro.regs.Status_Reg)->WP;
		ms_mspro_info->card_type = CARD_TYPE_MSPRO;
		
		return MS_MSPRO_NO_ERROR;
	}
	else
	{
		if (ms_mspro_buf->mspro.regs.Class_Reg & 0x03)           //Class_Reg = 0x01/0x02/0x03
		{
			ms_mspro_info->read_only_flag = 0x01;
			ms_mspro_info->card_type = CARD_TYPE_MSPRO;
			
			return MS_MSPRO_NO_ERROR;
		}
		else
		{
			return MS_MSPRO_ERROR_MEDIA_TYPE;
	}
}
}

int mspro_cpu_startup(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	int retry_cnt = 0;
	do
	{
		ms_delay_ms(10);
		
		retry_cnt++;
		
		packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
		packet.param.in.count = 1;
		packet.param.in.buffer = &ms_mspro_buf->mspro.regs.INT_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			continue;
			
		//check command status
		pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->mspro.regs.INT_Reg;
		if(!pIntReg->CED)
			continue;
			
		packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
		packet.param.in.count = 1;
		packet.param.in.buffer = &ms_mspro_buf->mspro.regs.INT_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			continue;

		if(pIntReg->ERR)
		{
			if(pIntReg->CMDNK)
			{
				ms_mspro_info->write_protected_flag = 1;
				break;
			}
			else
			{
				error = MSPRO_ERROR_MEDIA_BREAKDOWN;
				return error;
			}
		}
		else
		{
			break;
		}
		
	} while(retry_cnt < MSPRO_STARTUP_TIMEOUT);
	
	if(retry_cnt >= MSPRO_STARTUP_TIMEOUT)
		return MSPRO_ERROR_STARTUP_TIMEOUT;
		else
			return MS_MSPRO_NO_ERROR;
}

int mspro_confirm_attribute_information(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char * data_buf)
{
	int error,i;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	unsigned char entry_cnt = 0, entry_index = 0;

	// read attribute information
	error = mspro_read_attribute_sector(ms_mspro_info, 0, 1, data_buf);
	if(error)
		return error;
		
	// save attribute information area
	memcpy(&ms_mspro_buf->mspro.attribute_information_area, data_buf, MSRPO_ATTRIBUTE_INFOMATION_SIZE);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.attribute_information_area.Signature_Code);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.attribute_information_area.Version_Information);
	
	if(ms_mspro_buf->mspro.attribute_information_area.Signature_Code != 0xA5C3)
		return MSPRO_ERROR_MEDIA_BREAKDOWN;
	
	// check device information entry count
	entry_cnt = ms_mspro_buf->mspro.attribute_information_area.Device_Information_Entry_Count;
	if((entry_cnt < 1) || (entry_cnt > MSRPO_MAX_DEVICE_INFORMATION_ENTRY))
		return MSPRO_ERROR_MEDIA_BREAKDOWN;
	
	// check device information entry
	memcpy(&ms_mspro_buf->mspro.device_information_entry, data_buf+MSRPO_ATTRIBUTE_INFOMATION_SIZE, entry_cnt*MSRPO_DEVICE_INFORMATION_ENTRY_SIZE);
	error = MSPRO_ERROR_MEDIA_BREAKDOWN;
	for(i=0; i<entry_cnt; i++)
	{
		ms_mspro_endian_convert(ENDIAN_TYPE_DWORD, &ms_mspro_buf->mspro.device_information_entry[i].Address);
		ms_mspro_endian_convert(ENDIAN_TYPE_DWORD, &ms_mspro_buf->mspro.device_information_entry[i].Size);
		
		if(ms_mspro_buf->mspro.device_information_entry[i].Device_Information_ID != MSPRO_DEVID_SYSTEM_INFORMATION)
			continue;
			
		if(ms_mspro_buf->mspro.device_information_entry[i].Size != MSPRO_SYSTEM_INFORMATION_SIZE)
			break;
			
		if(ms_mspro_buf->mspro.device_information_entry[i].Address < 0x1A0)
			break;
		
		if((ms_mspro_buf->mspro.device_information_entry[i].Address + ms_mspro_buf->mspro.device_information_entry[i].Size) > 0x8000)
			break;
		
		entry_index = i;
		error = MS_MSPRO_NO_ERROR;
	}

	if(error)
		return error;
	
	ms_mspro_buf->mspro.system_entry_index = entry_index;
	
	return MS_MSPRO_NO_ERROR;
}

int mspro_confirm_system_information(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char * data_buf)
{
	int error;
	
	unsigned short sector_addr, data_offset, data_size;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	
	sector_addr = ms_mspro_buf->mspro.device_information_entry[ms_mspro_buf->mspro.system_entry_index].Address / MSPRO_SECTOR_SIZE;
	data_offset = ms_mspro_buf->mspro.device_information_entry[ms_mspro_buf->mspro.system_entry_index].Address % MSPRO_SECTOR_SIZE;
	data_size = ms_mspro_buf->mspro.device_information_entry[ms_mspro_buf->mspro.system_entry_index].Size;
	
	// read system information
	error = mspro_read_attribute_sector(ms_mspro_info, sector_addr, 1, data_buf);
	if(error)
		return error;
	// save system information
	memcpy(&ms_mspro_buf->mspro.system_information, data_buf+data_offset, MSPRO_SECTOR_SIZE-data_offset);
	
	if(data_size > (MSPRO_SECTOR_SIZE-data_offset))
	{
		// read system information
		error = mspro_read_attribute_sector(ms_mspro_info, sector_addr+1, 1, data_buf);
		if(error)
			return error;
		// save system information
		data_offset = data_size-(MSPRO_SECTOR_SIZE-data_offset)-1;
		memcpy((unsigned char *)&ms_mspro_buf->mspro.system_information+data_offset, data_buf, data_size-(MSPRO_SECTOR_SIZE-data_offset));
	}
	
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Block_Size);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Total_Blocks);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.User_Area_Blocks);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Page_Size);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Assembly_Date.Year);
	ms_mspro_endian_convert(ENDIAN_TYPE_DWORD, &ms_mspro_buf->mspro.system_information.Serial_Number);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Memory_Maker_Code);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Memory_Model_Code);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Controller_Number);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Controller_Function);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Start_Sector);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Unit_Size);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, &ms_mspro_buf->mspro.system_information.Controller_Code);
	
	// check system information
	if(ms_mspro_buf->mspro.system_information.Memory_Stick_Class != 2)
		return MSPRO_ERROR_MEDIA_BREAKDOWN;
	
	if(ms_mspro_buf->mspro.system_information.Device_Type == 0)
	{
		if(ms_mspro_info->read_only_flag)
			ms_mspro_info->read_only_flag = 0;
	}
	else if((ms_mspro_buf->mspro.system_information.Device_Type == 0x01) ||
		(ms_mspro_buf->mspro.system_information.Device_Type == 0x02) ||
		(ms_mspro_buf->mspro.system_information.Device_Type == 0x03))
	{
		if(!ms_mspro_info->read_only_flag)
			ms_mspro_info->read_only_flag = 1;
	}
	else
	{
		error = MSPRO_ERROR_MEDIA_BREAKDOWN;
		return error;
	}
	
	if((ms_mspro_buf->mspro.system_information.Memory_Stick_Sub_Class & 0xC0) != 0x00)
	{
		ms_mspro_info->write_protected_flag = 1;
	}
	
	return MS_MSPRO_NO_ERROR;
}

int mspro_recognize_file_system()
{
	return MS_MSPRO_NO_ERROR;
}

int mspro_read_user_sector(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long sector_addr, unsigned short sector_count, unsigned char * data_buf)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	unsigned long data_offset = 0;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);

	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;
		
	mass_counter=0;
	
	if(sector_count == 0)
		return MS_MSPRO_ERROR_PARAMETER;

	if((ms_mspro_buf->mspro.reg_set.write_addr != 0x11) ||
	   (ms_mspro_buf->mspro.reg_set.write_size != 0x06))
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->mspro.reg_set.write_addr = 0x11;
		ms_mspro_buf->mspro.reg_set.write_size = 0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->mspro.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
		packet.param.out.count = 6;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->mspro.regs.Data_Count_Reg1 = (sector_count>>8) & 0xFF;
		ms_mspro_buf->mspro.regs.Data_Count_Reg0 = sector_count & 0xFF;
		ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg3 = (sector_addr>>24) & 0xFF;
		ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg2 = (sector_addr>>16) & 0xFF;
		ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg1 = (sector_addr>>8) & 0xFF;
		ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg0 = sector_addr & 0xFF;
		packet.param.out.buffer = &ms_mspro_buf->mspro.regs.Data_Count_Reg1;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;

		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
		packet.param.out.count = 1;
		packet.param.out.buffer = buf;
		packet.param.out.buffer[0] = read_sector_type ? CMD_MSPRO_READ_ATRB : CMD_MSPRO_READ_DATA;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif 	
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{
		packet.TPC_cmd.value = TPC_MSPRO_EX_SET_CMD;        //EX_SET_CMD
		packet.param.out.count = 7;
		packet.param.out.buffer = buf;
		packet.param.out.buffer[0] = CMD_MSPRO_READ_DATA;
		packet.param.out.buffer[1] = (sector_count>>8) & 0xFF;
		packet.param.out.buffer[2] = sector_count & 0xFF;
		packet.param.out.buffer[3] = (sector_addr>>24) & 0xFF;
		packet.param.out.buffer[4] = (sector_addr>>16) & 0xFF;
		packet.param.out.buffer[5] = (sector_addr>>8) & 0xFF;
		packet.param.out.buffer[6] = sector_addr & 0xFF;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
}
#endif

	while(1)
	{
		error = ms_mspro_wait_int(ms_mspro_info, &packet);
		if(error)
			return error;
			
		//get INT register
		if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
		{
			ms_mspro_buf->mspro.regs.INT_Reg = packet.int_reg;
		}
		else
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
			packet.param.in.count = 1;
			packet.param.in.buffer = &ms_mspro_buf->mspro.regs.INT_Reg;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
		
		//check command status
		pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->mspro.regs.INT_Reg;
		if(pIntReg->CMDNK)
		{                   //BLOCK_READ CMD can not be excuted
			return MS_MSPRO_ERROR_CMDNK;
		}
		if(pIntReg->ERR && !pIntReg->BREQ)	// BREQ = 1 or ERR & BREQ = 1
		{
			return MS_MSPRO_ERROR_FLASH_READ;
		}
		if(pIntReg->CED)
		{
			if(mass_counter == 0) 
			{
			    return MS_MSPRO_ERROR_NO_READ;
			}
			else if(mass_counter != sector_count)
			{
				mspro_access_status_reg_after_read = 1;
			}
			break;
		}
		if (mass_counter == sector_count)
		{
			mass_counter = 0;
			return MS_MSPRO_ERROR_TIMEOUT;
		}	
		packet.TPC_cmd.value = TPC_MSPRO_READ_LONG_DATA;    //READ_LONG_DATA
		packet.param.in.count = MSPRO_SECTOR_SIZE;      //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.in.buffer = data_buf+data_offset;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;

		mass_counter++;

#ifdef AMLOGIC_CHIP_SUPPORT
		data_offset += ((unsigned long)data_buf == 0x3400000) ? 0 : MSPRO_SECTOR_SIZE;
#else
		data_offset += MSPRO_SECTOR_SIZE;
#endif
	}

	//for reset some m2 card state after read user sector
	if(mspro_access_status_reg_after_read)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //READ_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;
		ms_mspro_buf->ms.reg_set.read_addr = 0x02;           //READ_ADRS = 0x02
		ms_mspro_buf->ms.reg_set.read_size = 0x01;           //READ_SIZE = 0x06
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;

		packet.TPC_cmd.value = TPC_MS_MSPRO_READ_REG;       //READ_REG
		packet.param.in.count = 1;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.Status_Reg0;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}

	return MS_MSPRO_NO_ERROR;
}

int mspro_write_user_sector(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long sector_addr, unsigned short sector_count, unsigned char * data_buf)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	unsigned long data_offset = 0;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;

	mass_counter=0;

	if(sector_count == 0)
		return MS_MSPRO_ERROR_PARAMETER;

	if((ms_mspro_buf->mspro.reg_set.write_addr != 0x11) ||
	   (ms_mspro_buf->mspro.reg_set.write_size != 0x06))
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->mspro.reg_set.write_addr = 0x11;
		ms_mspro_buf->mspro.reg_set.write_size = 0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->mspro.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
		packet.param.out.count = 6;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->mspro.regs.Data_Count_Reg1 = (sector_count>>8) & 0xFF;
		ms_mspro_buf->mspro.regs.Data_Count_Reg0 = sector_count & 0xFF;
		ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg3 = (sector_addr>>24) & 0xFF;
		ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg2 = (sector_addr>>16) & 0xFF;
		ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg1 = (sector_addr>>8) & 0xFF;
		ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg0 = sector_addr & 0xFF;
		packet.param.out.buffer = &ms_mspro_buf->mspro.regs.Data_Count_Reg1;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;

		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
		packet.param.out.count = 1;
		packet.param.out.buffer = buf;
		packet.param.out.buffer[0] = CMD_MSPRO_WRITE_DATA;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif

#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{
		packet.TPC_cmd.value = TPC_MSPRO_EX_SET_CMD;        //EX_SET_CMD
		packet.param.out.count = 7;
		packet.param.out.buffer = buf;
		packet.param.out.buffer[0] = CMD_MSPRO_WRITE_DATA;
		packet.param.out.buffer[1] = (sector_count>>8) & 0xFF;
		packet.param.out.buffer[2] = sector_count & 0xFF;
		packet.param.out.buffer[3] = (sector_addr>>24) & 0xFF;
		packet.param.out.buffer[4] = (sector_addr>>16) & 0xFF;
		packet.param.out.buffer[5] = (sector_addr>>8) & 0xFF;
		packet.param.out.buffer[6] = sector_addr & 0xFF;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif 

	while(1)
	{
		error = ms_mspro_wait_int(ms_mspro_info, &packet);
		if(error)
			return error;
			
		//get INT register
		if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
		{
			ms_mspro_buf->mspro.regs.INT_Reg = packet.int_reg;
		}
		else
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
			packet.param.in.count = 1;
			packet.param.in.buffer = &ms_mspro_buf->mspro.regs.INT_Reg;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
	}
		
		//check command status
		pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->mspro.regs.INT_Reg;
		if(pIntReg->CED && pIntReg->ERR && pIntReg->CMDNK)
		{
			return MSPRO_ERROR_WRITE_DISABLED;
	}
		else if (pIntReg->CMDNK)
		{
			return MS_MSPRO_ERROR_CMDNK;
	}
		if(pIntReg->ERR && pIntReg->CED)
		{
			return MS_MSPRO_ERROR_FLASH_WRITE;
	}
		if(pIntReg->CED)
		{
			break;
		}
		
		packet.TPC_cmd.value = TPC_MSPRO_WRITE_LONG_DATA;   //WRITE_LONG_DATA
		packet.param.out.count = MSPRO_SECTOR_SIZE;     //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.out.buffer = data_buf+data_offset;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;

		mass_counter++;

		data_offset += MSPRO_SECTOR_SIZE;
	}

	return MS_MSPRO_NO_ERROR;
}

int mspro_erase_user_sector(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long sector_addr, unsigned short sector_count)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	//unsigned char report_buf[MSPRO_REPORT_SIZE];
	unsigned long all_sectorts = 0, processed_sectors = 0;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);

	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;
	unsigned char* report_buf = ms_mspro_info->data_buf;
	
	memset(report_buf, 0, MSPRO_REPORT_SIZE);
		
	if(sector_count == 0)
		return MS_MSPRO_ERROR_PARAMETER;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		if((ms_mspro_buf->mspro.reg_set.write_addr != 0x11) ||
	   	   (ms_mspro_buf->mspro.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->mspro.reg_set.write_addr = 0x11;
			ms_mspro_buf->mspro.reg_set.write_size = 0x06;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->mspro.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}	   	  
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{
		if((ms_mspro_buf->mspro.reg_set.write_addr != 0x11) ||
	   	   (ms_mspro_buf->mspro.reg_set.write_size != 0x07))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->mspro.reg_set.write_addr = 0x11;
			ms_mspro_buf->mspro.reg_set.write_size = 0x07;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->mspro.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
#endif

	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		packet.param.out.count = 6;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		packet.param.out.count = 7;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
	ms_mspro_buf->mspro.regs.Data_Count_Reg1 = (sector_count>>8) & 0xFF;
	ms_mspro_buf->mspro.regs.Data_Count_Reg0 = sector_count & 0xFF;
	ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg3 = (sector_addr>>24) & 0xFF;
	ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg2 = (sector_addr>>16) & 0xFF;
	ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg1 = (sector_addr>>8) & 0xFF;
	ms_mspro_buf->mspro.regs.parameters.data.Data_Address_Reg0 = sector_addr & 0xFF;
	ms_mspro_buf->mspro.regs.parameters.data.TPC_Pamameter_Reg = MSPRO_REPORT_TYPE;
	packet.param.out.buffer = &ms_mspro_buf->mspro.regs.Data_Count_Reg1;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->mspro.reg_set.write_addr = 0x17;
		ms_mspro_buf->mspro.reg_set.write_size = 0x01;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->mspro.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	
		packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 1;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->mspro.regs.parameters.data.TPC_Pamameter_Reg = MSPRO_REPORT_TYPE;
		packet.param.out.buffer = &ms_mspro_buf->mspro.regs.parameters.data.TPC_Pamameter_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif

	//why not use EXT_SET_CMD here? Because EXT_SET_CMD can only support 6 bytes register parameters
	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MSPRO_ERASE;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	while(1)
	{
		error = ms_mspro_wait_int(ms_mspro_info, &packet);
		if(error)
			return error;
			
		//get INT register
		if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
		{
			ms_mspro_buf->mspro.regs.INT_Reg = packet.int_reg;
		}
		else
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
			packet.param.in.count = 1;
			packet.param.in.buffer = &ms_mspro_buf->mspro.regs.INT_Reg;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
		
		//check command status
		pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->mspro.regs.INT_Reg;
		if(pIntReg->CMDNK)
		{                   //BLOCK_READ CMD can not be excuted
			return MS_MSPRO_ERROR_CMDNK;
		}
		if(pIntReg->ERR && pIntReg->CED)
		{
			return MS_MSPRO_ERROR_FLASH_ERASE;
		}
		if(pIntReg->CED)
		{
			break;
		}
		
		packet.TPC_cmd.value = TPC_MSPRO_READ_SHORT_DATA;   //READ_SHORT_DATA
		packet.param.in.count = MSPRO_REPORT_SIZE;      //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.in.buffer = report_buf;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;

		all_sectorts = (report_buf[0]<<24) | (report_buf[1]<<16) | (report_buf[2]<<8) | report_buf[3];
		processed_sectors = (report_buf[4]<<24) | (report_buf[5]<<16) | (report_buf[6]<<8) | report_buf[7];
	}
	
	if(all_sectorts != processed_sectors)
		return MS_MSPRO_ERROR_FLASH_ERASE;
	else
		return MS_MSPRO_NO_ERROR;
}

int mspro_read_attribute_sector(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long sector_addr, unsigned short sector_count, unsigned char * data_buf)
{
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		int error,tracer;
		read_sector_type=1;
		error = mspro_read_user_sector(ms_mspro_info, sector_addr, sector_count, data_buf);
		tracer=0;
		while (1)
		{
			if (error == 0)
				break;
			if (mass_counter == 0)
				tracer++;
			else
				tracer=0;
			if (tracer>3)
				break;
			sector_addr += mass_counter;
			sector_count -= mass_counter;
			data_buf += MSPRO_SECTOR_SIZE*mass_counter;
			error = mspro_read_user_sector(ms_mspro_info, sector_addr, sector_count, data_buf);
		}	
		read_sector_type=0;
		return error;
	}
#endif

#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{
		
	MS_MSPRO_TPC_Packet_t packet;
	unsigned long data_offset = 0;
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;
		
	if(sector_count == 0)
		return MS_MSPRO_ERROR_PARAMETER;

	if((ms_mspro_buf->mspro.reg_set.write_addr != 0x11) ||
	   (ms_mspro_buf->mspro.reg_set.write_size != 0x06))
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->mspro.reg_set.write_addr = 0x11;
		ms_mspro_buf->mspro.reg_set.write_size = 0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->mspro.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}

	packet.TPC_cmd.value = TPC_MSPRO_EX_SET_CMD;        //EX_SET_CMD
	packet.param.out.count = 7;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MSPRO_READ_ATRB;
	packet.param.out.buffer[1] = 0;
	packet.param.out.buffer[2] = sector_count;
	packet.param.out.buffer[3] = 0;
	packet.param.out.buffer[4] = 0;
	packet.param.out.buffer[5] = 0;
	packet.param.out.buffer[6] = sector_addr;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	while(1)
	{
		error = ms_mspro_wait_int(ms_mspro_info, &packet);
		if(error)
			return error;
			
		//get INT register
		if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
		{
			ms_mspro_buf->mspro.regs.INT_Reg = packet.int_reg;
		}
		else
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
			packet.param.in.count = 1;
			packet.param.in.buffer = &ms_mspro_buf->mspro.regs.INT_Reg;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
		
		//check command status
		pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->mspro.regs.INT_Reg;
		if(pIntReg->CMDNK)
		{                   //BLOCK_READ CMD can not be excuted
			return MS_MSPRO_ERROR_CMDNK;
		}
		if(pIntReg->ERR && !pIntReg->BREQ)
		{
			return MS_MSPRO_ERROR_FLASH_READ;
		}
		if(pIntReg->CED)
		{
			break;
		}
		
		packet.TPC_cmd.value = TPC_MSPRO_READ_LONG_DATA;    //READ_LONG_DATA
		packet.param.in.count = MSPRO_SECTOR_SIZE;      //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.in.buffer = data_buf+data_offset;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;

		data_offset += MSPRO_SECTOR_SIZE;
	}

	return MS_MSPRO_NO_ERROR;
	
	}
#endif
	return 0;
}

int mspro_read_information_block()
{
	return 0;
}

int mspro_update_imformation_block()
{
	return 0;
}

int mspro_format()
{
	return 0;
}

int mspro_sleep()
{
	return 0;
}
