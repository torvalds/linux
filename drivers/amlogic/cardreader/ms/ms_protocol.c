#include "ms_port.h"
#include "ms_misc.h"
#include "ms_mspro.h"
#include "ms_protocol.h"

extern unsigned check_one_boot_block;

int ms_media_type_identification(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	if((ms_mspro_buf->ms.regs.Type_Reg >= 0x01) && (ms_mspro_buf->ms.regs.Type_Reg <= 0xFE))
		return MS_MSPRO_ERROR_MEDIA_TYPE;
		
	if(ms_mspro_buf->ms.regs.Type_Reg != 0x00)
	{
		if((ms_mspro_buf->ms.regs.Category_Reg == 0x00) ||
		   ((ms_mspro_buf->ms.regs.Category_Reg >= 0x80) && (ms_mspro_buf->ms.regs.Category_Reg <= 0xFE)))
			return MS_MSPRO_ERROR_MEDIA_TYPE;
			
		if((ms_mspro_buf->ms.regs.Category_Reg >= 0x01) && (ms_mspro_buf->ms.regs.Category_Reg <= 0x7F))
		{
			ms_mspro_info->card_type = CARD_TYPE_MS;     //Memory Stick I/O expanded module
			return MS_MSPRO_NO_ERROR;
		}
		
		if((ms_mspro_buf->ms.regs.Class_Reg == 0x00) ||
		   ((ms_mspro_buf->ms.regs.Class_Reg >= 0x04) && (ms_mspro_buf->ms.regs.Class_Reg <= 0xFE)))
			return MS_MSPRO_ERROR_MEDIA_TYPE;
	}
	else
	{
		if(ms_mspro_buf->ms.regs.Category_Reg >= 0x80)
			return MS_MSPRO_ERROR_MEDIA_TYPE;
			
		if(ms_mspro_buf->ms.regs.Category_Reg >= 0x01)
		{
			ms_mspro_info->card_type = CARD_TYPE_MS;     //Memory Stick I/O expanded module
			return MS_MSPRO_NO_ERROR;
		}
		
		if(ms_mspro_buf->ms.regs.Class_Reg >= 0x04)
			return MS_MSPRO_ERROR_MEDIA_TYPE;
	}
	
	if((ms_mspro_buf->ms.regs.Class_Reg >= 0x01) && (ms_mspro_buf->ms.regs.Class_Reg <= 0x03))        //always in Write_Protected status
		ms_mspro_info->read_only_flag =  0x01;
	else
		ms_mspro_info->write_protected_flag = ((MS_Status_Register0_t*)&ms_mspro_buf->ms.regs.Status_Reg0)->WP;
		
	ms_mspro_info->card_type = CARD_TYPE_MS;
	
	return MS_MSPRO_NO_ERROR;
}

int ms_search_boot_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char * data_buf)
{
	int i;
	
	int error;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	ms_mspro_buf->ms.boot_area_protection_process_flag = 0;
	ms_mspro_buf->ms.boot_block_nums = 0;
	
	for(i=0; i<=12; i++)
	{
		error = ms_read_page(ms_mspro_info, i, 0, data_buf);
		if(error)
			continue;
			
		if(!((MS_Overwrite_Flag_Register_t*)&ms_mspro_buf->ms.regs.Overwrite_Flag_Reg)->BKST)
			continue;
			
		if(((MS_Management_Flag_Register_t*)&ms_mspro_buf->ms.regs.Management_Flag_Reg)->SYSFLG)
		{
			ms_mspro_buf->ms.boot_area_protection_process_flag = 1;
			continue;
		}
		
		if(!((data_buf[0]==0x00) && (data_buf[1]==0x01)))
		{
			ms_mspro_buf->ms.boot_area_protection_process_flag = 1;
			continue;
		}
		
		//Boot Block Found!
		ms_mspro_buf->ms.boot_block_no[ms_mspro_buf->ms.boot_block_nums] = i;
		ms_mspro_buf->ms.boot_block_nums++;
		
		if((ms_mspro_buf->ms.boot_block_nums == 1)&&(check_one_boot_block == 1))
			break;			
		if(ms_mspro_buf->ms.boot_block_nums >= 2)
			break;
	}
	
	if((ms_mspro_buf->ms.boot_block_nums == 1)&&(check_one_boot_block == 0))
	{
		check_one_boot_block = 1;
	}
	
	if((ms_mspro_buf->ms.boot_block_nums == 2) && (ms_mspro_buf->ms.boot_block_no[0] > ms_mspro_buf->ms.boot_block_no[1]))
	{
		i = ms_mspro_buf->ms.boot_block_no[0];
		ms_mspro_buf->ms.boot_block_no[0] = ms_mspro_buf->ms.boot_block_no[1];
		ms_mspro_buf->ms.boot_block_no[1] = i;
	}
	
	if(ms_mspro_buf->ms.boot_block_nums == 0)
		return MS_ERROR_BOOT_SEARCH;
	else
		return MS_MSPRO_NO_ERROR;
}

int ms_check_boot_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char * data_buf)
{
	int error;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	// Acquire Boot & Attribute Information form effective Boot Block
	error = ms_read_page(ms_mspro_info, ms_mspro_buf->ms.boot_block_no[0], 0, data_buf);
	if(error)
		return error;
		
	// header
	memcpy(&ms_mspro_buf->ms.boot_header.Block_ID, data_buf, 2);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_header.Block_ID) ;
	ms_mspro_buf->ms.boot_header.Data_Entry = data_buf[188] ;

	// system entry
	memcpy(&ms_mspro_buf->ms.boot_system_entry, &data_buf[MS_BOOT_HEADER_SIZE], MS_BOOT_SYSTEM_ENTRY_SIZE);
	ms_mspro_endian_convert(ENDIAN_TYPE_DWORD, (unsigned char*)&ms_mspro_buf->ms.boot_system_entry.Disabled_Block_Start_Address);
	ms_mspro_endian_convert(ENDIAN_TYPE_DWORD, (unsigned char*)&ms_mspro_buf->ms.boot_system_entry.Disabled_Block_Data_Size);
	ms_mspro_endian_convert(ENDIAN_TYPE_DWORD, (unsigned char*)&ms_mspro_buf->ms.boot_system_entry.CIS_IDI_Start_Address);
	ms_mspro_endian_convert(ENDIAN_TYPE_DWORD, (unsigned char*)&ms_mspro_buf->ms.boot_system_entry.CIS_IDI_Data_Size);

	// boot & attribute information
	memcpy(&ms_mspro_buf->ms.boot_attribute_information, &data_buf[MS_BOOT_HEADER_SIZE+MS_BOOT_SYSTEM_ENTRY_SIZE], MS_BOOT_ATTRIBUTE_INFORMATION_SIZE);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Block_Size);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Block_Numbers);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Effective_Block_Numbers);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Page_Size);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Assembly_Date_Time.AD);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Memory_Manufacturer_Code);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Memory_Device_Code);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Implemented_Capacity);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Controller_Number);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Controller_Function);
	ms_mspro_endian_convert(ENDIAN_TYPE_WORD, (unsigned char*)&ms_mspro_buf->ms.boot_attribute_information.Format_Unique_Value6);
	
	// check Memory Stick Class
	if(ms_mspro_buf->ms.boot_attribute_information.Memory_Stick_Class != 1)
		return MS_ERROR_MEMORY_STICK_TYPE;
	
	// Confirm "Device Type", the value of "Parallel transfer support", and the register value of acquired from "Media Type Indeification Process"
	if(ms_mspro_buf->ms.regs.Type_Reg == 0x00)
	{
		if(ms_mspro_buf->ms.regs.Category_Reg == 0x00)
		{
			if(ms_mspro_buf->ms.regs.Class_Reg >= 0x04)
				error = MS_ERROR_MEMORY_STICK_TYPE;
			else if(ms_mspro_buf->ms.boot_attribute_information.Parallel_Transfer_Supporting & 0xFE) // != 0,1
				error = MS_ERROR_MEMORY_STICK_TYPE;
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x00)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 0) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x01)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x02)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 2) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x03)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 2) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
		}
		else if(ms_mspro_buf->ms.regs.Category_Reg >= 0x80)
			error = MS_ERROR_MEMORY_STICK_TYPE;
	}
	else if(ms_mspro_buf->ms.regs.Type_Reg == 0x01)
	{
		if(ms_mspro_buf->ms.regs.Category_Reg == 0x00)
		{
			if(ms_mspro_buf->ms.regs.Class_Reg >= 0x04)
				error = MS_ERROR_MEMORY_STICK_TYPE;
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x00)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 0) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 2) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x01)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x02)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 2) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x03)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 2) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
		}
	}
	else if((ms_mspro_buf->ms.regs.Type_Reg == 0x02) && (ms_mspro_buf->ms.regs.Type_Reg <= 0xFE))
	{
		error = MS_ERROR_MEMORY_STICK_TYPE;
	}
	else if(ms_mspro_buf->ms.regs.Type_Reg == 0xFF)
	{
		if(ms_mspro_buf->ms.regs.Category_Reg == 0x00)
			error = MS_ERROR_MEMORY_STICK_TYPE;
		else if((ms_mspro_buf->ms.regs.Category_Reg >= 0x80) && (ms_mspro_buf->ms.regs.Category_Reg <= 0xFE))
			error = MS_ERROR_MEMORY_STICK_TYPE;
		else if(ms_mspro_buf->ms.regs.Category_Reg == 0xFF)
		{
			if(ms_mspro_buf->ms.regs.Class_Reg == 0x00)
				error = MS_ERROR_MEMORY_STICK_TYPE;
			else if((ms_mspro_buf->ms.regs.Class_Reg >= 0x04) && (ms_mspro_buf->ms.regs.Class_Reg <= 0xFE))
				error = MS_ERROR_MEMORY_STICK_TYPE;
			else if(ms_mspro_buf->ms.boot_attribute_information.Parallel_Transfer_Supporting != 0)
				error = MS_ERROR_MEMORY_STICK_TYPE;
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x01)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x02)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 2) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0x03)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 2) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
			else if(ms_mspro_buf->ms.regs.Class_Reg == 0xFF)
			{
				if ((ms_mspro_buf->ms.boot_attribute_information.Device_Type != 0) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 1) &&
				   (ms_mspro_buf->ms.boot_attribute_information.Device_Type != 3))
				   error = MS_ERROR_MEMORY_STICK_TYPE;
			}
		}
	}
	if(error)
		return error;
	
	// check Format Type
	if(ms_mspro_buf->ms.boot_attribute_information.Format_Type != 1)
		return MS_ERROR_FORMAT_TYPE;
	
	// Confirm number blocks, effective blocks and block size
	if(!(( //4MB
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Numbers == 0x0200) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Effective_Block_Numbers == 0x01f0) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Size == 0x08)
		  ) ||
		  ( //8MB
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Numbers == 0x0400) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Effective_Block_Numbers == 0x03e0) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Size == 0x08)
		  ) ||
		  ( //16MB
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Numbers == 0x0400) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Effective_Block_Numbers == 0x03e0) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Size == 0x10)
		  ) ||
		  ( //32MB
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Numbers == 0x0800) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Effective_Block_Numbers == 0x07c0) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Size == 0x10)
		  ) ||
		  ( //64MB
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Numbers == 0x1000) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Effective_Block_Numbers == 0x0f80) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Size == 0x10)
		  )||
		  ( //128MB
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Numbers == 0x2000) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Effective_Block_Numbers == 0x1f00) &&
		   (ms_mspro_buf->ms.boot_attribute_information.Block_Size == 0x10)
		  )
	   ))
	{
		return MS_ERROR_BLOCK_NUMBER_SIZE;
	}
	
	if((ms_mspro_buf->ms.boot_attribute_information.Controller_Function & 0xFF00) == 0x1000)
	{
#ifdef  MS_MSPRO_DEBUG
		Debug_Printf("Memory Stick MagicGate compliant media.\n");
#endif
	}

	return MS_MSPRO_NO_ERROR;
}

int ms_check_disabled_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char * data_buf)
{
	int error;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	unsigned short block_no,i;
	
	// Acquire Boot & Attribute Information form effective Boot Block
	error = ms_read_page(ms_mspro_info, ms_mspro_buf->ms.boot_block_no[0], 1, data_buf);
	if(error)
		return MS_ERROR_DISABLED_BLOCK;
	
	// big-endian
	for(i=0; i<MS_MAX_DISABLED_BLOCKS; i++)
	{
		block_no = data_buf[i*2+0] << 8;
		block_no |= data_buf[i*2+1];
		
		ms_mspro_buf->ms.disabled_block_data.disabled_block_table[i] = block_no;
		
		if (block_no == 0xFFFF)
			break;
	}
	
	ms_mspro_buf->ms.disabled_block_data.disabled_block_nums = i;
	
	return MS_MSPRO_NO_ERROR;
}

int ms_boot_area_protection(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned char * data_buf)
{
	int error,i,j,k;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	unsigned char last_block_no = 0;
	unsigned long pages_per_block = ms_mspro_buf->ms.boot_attribute_information.Block_Size*2;
	
	if(ms_mspro_buf->ms.boot_block_nums == 0)
		return MS_ERROR_BOOT_SEARCH;
	else if(ms_mspro_buf->ms.boot_block_nums == 1)
		last_block_no = ms_mspro_buf->ms.boot_block_no[0];
	else if(ms_mspro_buf->ms.boot_block_nums == 2)
		last_block_no = ms_mspro_buf->ms.boot_block_no[1];
		
	for(i=0; i<=last_block_no; i++)
	{
		if(ms_mspro_buf->ms.boot_block_nums == 1)
		{
			if(i == ms_mspro_buf->ms.boot_block_no[0])
				continue;
		}
		else if(ms_mspro_buf->ms.boot_block_nums == 2)
		{
			if((i == ms_mspro_buf->ms.boot_block_no[0]) || (i == ms_mspro_buf->ms.boot_block_no[1]))
				continue;
		}
		
		error = ms_read_page(ms_mspro_info, i, 0, data_buf);
		if(error)
			continue;
			
		if(((MS_Overwrite_Flag_Register_t*)&ms_mspro_buf->ms.regs.Overwrite_Flag_Reg)->BKST == 0)
			continue;
			
		if(!((ms_mspro_buf->ms.regs.Logical_Address_Reg1 == 0xFF) && (ms_mspro_buf->ms.regs.Logical_Address_Reg0 == 0xFF)))
		{
			// copy the contents of this block to User Area (alternative block)
			for(j=(last_block_no+1); j<(MS_BLOCKS_PER_SEGMENT-2); j++)
			{
				error = ms_read_page(ms_mspro_info, j, 0, data_buf);
				if(error)
					continue;
					
				if((ms_mspro_buf->ms.regs.Logical_Address_Reg1 == 0xFF) && (ms_mspro_buf->ms.regs.Logical_Address_Reg0 == 0xFF))
				{
					for(k=0; k<pages_per_block; k++)
					{
						error = ms_copy_page(ms_mspro_info, i, k, j, k, data_buf);
						if(error)
							break;
					}
					if(error)
						continue;
					else
						break;
				}
			}
		}
		
		ms_overwrite_extra_data(ms_mspro_info, i, 0, 0x7F);
	}
	
	return MS_MSPRO_NO_ERROR;
}

#if 0
int ms_read_boot_idi(unsigned char * data_buf)
{
	int error;
	
	if(ms_mspro_buf->ms.boot_system_entry.CIS_IDI_Data_Type_ID != 0x0A)
		return MS_ERROR_BOOT_IDI;
		
	// Acquire Boot IDI Information
	error = ms_read_page(ms_mspro_buf->ms.boot_block_no[0], 2, data_buf);
	if(error)
		return error;
		
	memcpy(&ms_mspro_buf->ms.boot_idi, data_buf+MS_BOOT_CIS_SIZE, ms_mspro_buf->ms.boot_system_entry.CIS_IDI_Data_Size);
	
	return MS_MSPRO_NO_ERROR;
}
#endif

static unsigned short logical_physical_table[MS_LOGICAL_SIZE_PER_SEGMENT], free_table[MS_BLOCKS_PER_SEGMENT];
int ms_logical_physical_table_creation(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned short seg_no)
{
	int error;
	unsigned char US;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	//unsigned short logical_physical_table[MS_LOGICAL_SIZE_PER_SEGMENT], free_table[MS_BLOCKS_PER_SEGMENT];
	unsigned long physical_blk_no,last_seg_no;
	unsigned short free_table_data_nums,logical_address;
	unsigned short i;
	
	unsigned short start_logical_addr,end_logical_addr;
	if(seg_no == 0)
	{
		start_logical_addr = 0;
		end_logical_addr = MS_LOGICAL_SIZE_PER_SEGMENT-2-1;
	}
	else
	{
		start_logical_addr = seg_no*MS_LOGICAL_SIZE_PER_SEGMENT-2;
		end_logical_addr = (seg_no+1)*MS_LOGICAL_SIZE_PER_SEGMENT-2-1;
	}
			
	last_seg_no = ms_mspro_buf->ms.boot_attribute_information.Block_Numbers/MS_BLOCKS_PER_SEGMENT-1;
	
	// Logical Address / Physical Block Number Corresponding information creation process
	
	for(i=0; i<MS_LOGICAL_SIZE_PER_SEGMENT; i++)
		logical_physical_table[i] = 0xFFFF;
		
	for(i=0; i<MS_BLOCKS_PER_SEGMENT; i++)
		free_table[i] = 0xFFFF;
	
	free_table_data_nums = 0;
	
	for(physical_blk_no=(seg_no*MS_BLOCKS_PER_SEGMENT); physical_blk_no<((seg_no+1)*MS_BLOCKS_PER_SEGMENT); physical_blk_no++)
	{
		if((ms_mspro_buf->ms.boot_block_nums == 1) && (physical_blk_no == ms_mspro_buf->ms.boot_block_no[0]))
			continue;
			
		if((ms_mspro_buf->ms.boot_block_nums == 2) && ((physical_blk_no == ms_mspro_buf->ms.boot_block_no[0]) || (physical_blk_no == ms_mspro_buf->ms.boot_block_no[1])))
			continue;
			
		for(i=0; i<ms_mspro_buf->ms.disabled_block_data.disabled_block_nums; i++)
		{
			if(physical_blk_no == ms_mspro_buf->ms.disabled_block_data.disabled_block_table[i])
				break;
		}
		if(i != ms_mspro_buf->ms.disabled_block_data.disabled_block_nums)
			continue;
			
		error = ms_read_extra_data(ms_mspro_info, physical_blk_no, 0);
		if(error)
		{
			if(error == MS_MSPRO_ERROR_FLASH_READ)
				ms_overwrite_extra_data(ms_mspro_info, physical_blk_no, 0, ms_mspro_buf->ms.regs.Overwrite_Flag_Reg&0x7F);
			continue;
		}
				
		if((seg_no == last_seg_no) &&
		  !((MS_Management_Flag_Register_t*)(&ms_mspro_buf->ms.regs.Management_Flag_Reg))->ATFLG)
		{
			ms_erase_block(ms_mspro_info, physical_blk_no);
		}
		
		if(((MS_Overwrite_Flag_Register_t*)(&ms_mspro_buf->ms.regs.Overwrite_Flag_Reg))->BKST)
		{
			unsigned char PS = ((MS_Overwrite_Flag_Register_t*)(&ms_mspro_buf->ms.regs.Overwrite_Flag_Reg))->PGST1<<1 | ((MS_Overwrite_Flag_Register_t*)(&ms_mspro_buf->ms.regs.Overwrite_Flag_Reg))->PGST0;
			if((PS != 0) && (PS != 3))
			{
				ms_overwrite_extra_data(ms_mspro_info, physical_blk_no, 0, ms_mspro_buf->ms.regs.Overwrite_Flag_Reg&0x7F);
				continue;
			}
			
			logical_address = ms_mspro_buf->ms.regs.Logical_Address_Reg1<<8 | ms_mspro_buf->ms.regs.Logical_Address_Reg0;
			if((logical_address == 0xFFFF) || (logical_address < start_logical_addr) || (logical_address > end_logical_addr))
			{
				ms_erase_block(ms_mspro_info, physical_blk_no);
				free_table[free_table_data_nums++] = physical_blk_no;
				continue;
			}
			
			if(logical_physical_table[logical_address-start_logical_addr] == 0xFFFF)
			{
				logical_physical_table[logical_address-start_logical_addr] = physical_blk_no;
				continue;
			}
			
			US = ((MS_Overwrite_Flag_Register_t*)(&ms_mspro_buf->ms.regs.Overwrite_Flag_Reg))->UDST;
			error = ms_read_extra_data(ms_mspro_info, physical_blk_no, 0);
			if(error)
			{
				continue;
			}
			if(US == ((MS_Overwrite_Flag_Register_t*)(&ms_mspro_buf->ms.regs.Overwrite_Flag_Reg))->UDST)
			{
				if(physical_blk_no < logical_physical_table[logical_address-start_logical_addr])
				{
					ms_erase_block(ms_mspro_info, physical_blk_no);
					free_table[free_table_data_nums++] = physical_blk_no;
				}
				else
				{
					ms_erase_block(ms_mspro_info, logical_physical_table[logical_address-start_logical_addr]);
					free_table[free_table_data_nums++] = logical_physical_table[logical_address-start_logical_addr];
					logical_physical_table[logical_address-start_logical_addr] = physical_blk_no;
				}
			}
			else
			{
				if(US == 0)
				{
					ms_erase_block(ms_mspro_info, logical_physical_table[logical_address-start_logical_addr]);
					free_table[free_table_data_nums++] = logical_physical_table[logical_address-start_logical_addr];
					logical_physical_table[logical_address-start_logical_addr] = physical_blk_no;
				}
				else
				{
					ms_erase_block(ms_mspro_info, physical_blk_no);
					free_table[free_table_data_nums++] = physical_blk_no;
				}
			}
		}
	}
	
	// Logical Address Confirmation Process
	for(logical_address=start_logical_addr; logical_address<=end_logical_addr; logical_address++)
	{
		if(((seg_no == last_seg_no) && (free_table_data_nums < 2)) ||
		  ((seg_no != last_seg_no) && (free_table_data_nums < 1)))
			break;
	  
		if(logical_physical_table[logical_address-start_logical_addr] == 0xFFFF)
		{
			error = ms_read_extra_data(ms_mspro_info, free_table[free_table_data_nums-1], 0);
			if(error)
			{
				free_table_data_nums--;
			}
			else
			{
				ms_mspro_buf->ms.regs.Logical_Address_Reg1 = (logical_address >> 8) & 0xFF;
				ms_mspro_buf->ms.regs.Logical_Address_Reg0 = logical_address & 0xFF;
				error = ms_write_extra_data(ms_mspro_info, free_table[free_table_data_nums-1], 0);
					
				logical_physical_table[logical_address-start_logical_addr] = free_table[free_table_data_nums-1];
				free_table[free_table_data_nums-1] = 0xFFFF;
				free_table_data_nums--;
			}
		}
	}

	for(logical_address=start_logical_addr; logical_address<=end_logical_addr; logical_address++)
	{
		ms_mspro_buf->ms.logical_physical_table[logical_address] = logical_physical_table[logical_address-start_logical_addr];
	}
	for(i=0; i<MS_MAX_FREE_BLOCKS_PER_SEGMENT; i++)
	{
		ms_mspro_buf->ms.free_block_table[seg_no*MS_MAX_FREE_BLOCKS_PER_SEGMENT+i] = free_table[i];
	}
	

	for(logical_address=start_logical_addr; logical_address<=end_logical_addr; logical_address++)
	{
		if(logical_physical_table[logical_address-start_logical_addr] == 0xFFFF)
		{
#ifdef MS_MSPRO_DEBUG
			Debug_Printf("Error logical block %d, segment %d\n", logical_address-start_logical_addr, seg_no);
#endif
			//return MS_ERROR_LOGICAL_PHYSICAL_TABLE;
		}
	}

	return MS_MSPRO_NO_ERROR;
}

int ms_read_page(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr, unsigned char * data_buf)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_Status_Register1_t * pStatusReg1;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.read_addr != 0x01) ||
	  	  (ms_mspro_buf->ms.reg_set.read_size != 0x10) ||
	   	  (ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	  (ms_mspro_buf->ms.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.read_addr = 0x01;
			ms_mspro_buf->ms.reg_set.read_size = 0x10;
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x06;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}	   	  
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.read_addr != 0x01) ||
	   	  (ms_mspro_buf->ms.reg_set.read_size != 0x19) ||
	   	  (ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	  (ms_mspro_buf->ms.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.read_addr = 0x01;
			ms_mspro_buf->ms.reg_set.read_size = 0x19;
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x06;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
#endif
	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
	packet.param.out.count = 6;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = block_addr & 0xFF;
	ms_mspro_buf->ms.regs.CMD_Parameter_Reg = 0x20;
	ms_mspro_buf->ms.regs.Page_Address_Reg = page_addr;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_READ;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_mspro_wait_int(ms_mspro_info, &packet);
	if(error)
		return error;

	packet.TPC_cmd.value = TPC_MS_MSPRO_READ_REG;       //READ_REG
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		packet.param.in.count = 0x10;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		packet.param.in.count = 0x19;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
	packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.read_addr = 0x01+0x10;
		ms_mspro_buf->ms.reg_set.read_size = 0x19-0x10;
		ms_mspro_buf->ms.reg_set.write_addr = 0x10;
		ms_mspro_buf->ms.reg_set.write_size = 0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
		
		packet.TPC_cmd.value = TPC_MS_MSPRO_READ_REG;       //READ_REG
		packet.param.in.count = 0x19-0x10;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.Block_Address_Reg2;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif
	
	//get INT register
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
	}

	//check command status
	pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
	if(pIntReg->CMDNK)                  //BLOCK_READ CMD can not be excuted
		return MS_MSPRO_ERROR_CMDNK;

	pStatusReg1 = (MS_Status_Register1_t*)&ms_mspro_buf->ms.regs.Status_Reg1;
	if(!(pIntReg->CED & pIntReg->BREQ))
		return MS_MSPRO_ERROR_FLASH_READ;
	
	if(pIntReg->ERR &&
	   (pStatusReg1->UCDT | pStatusReg1->UCEX | pStatusReg1->UCFG))
		return MS_MSPRO_ERROR_FLASH_READ;
	
	packet.TPC_cmd.value = TPC_MS_READ_PAGE_DATA;       //READ_PAGE_DATA
	packet.param.in.count = MS_PAGE_SIZE;           //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	packet.param.in.buffer = data_buf;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	return MS_MSPRO_NO_ERROR;
}

#ifdef MS_WRITE_PATTERN_1
//pattern 1
int ms_write_page(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr, unsigned char * data_buf)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	  	  (ms_mspro_buf->ms.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x06;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}  	  
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	  (ms_mspro_buf->ms.reg_set.write_size != 0x0A))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x0A;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
#endif
	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		packet.param.out.count = 0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		packet.param.out.count = 0x0A;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = block_addr & 0xFF;
	ms_mspro_buf->ms.regs.CMD_Parameter_Reg = 0x20;
	ms_mspro_buf->ms.regs.Page_Address_Reg = page_addr;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.write_addr = 0x10+0x06;
		ms_mspro_buf->ms.reg_set.write_size = 0x0A-0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
		
		packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
		packet.param.out.count = 0x0A-0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.out.buffer = &ms_mspro_buf->ms.regs.Overwrite_Flag_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif
			
	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_WRITE;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_mspro_wait_int(ms_mspro_info, &packet);
	if(error)
		return error;
		
	//get INT register
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
	}
	else
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
		packet.param.in.count = 1;
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
		
	//check command status
	MS_MSPRO_INT_Register_t * pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
	if(pIntReg->CMDNK)                  //BLOCK_READ CMD can not be excuted
		return MS_MSPRO_ERROR_CMDNK;
		
	if(!pIntReg->BREQ)                  //BLOCK_READ CMD can not be excuted
		return MS_MSPRO_ERROR_FLASH_WRITE;
	
	packet.TPC_cmd.value = TPC_MS_WRITE_PAGE_DATA;      //WRITE_PAGE_DATA
	packet.param.out.count = MS_PAGE_SIZE;          //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	packet.param.out.buffer = data_buf;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_mspro_wait_int(ms_mspro_info, &packet);
	if(error)
		return error;
		
	//get INT register
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
	}
	else
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
		packet.param.in.count = 1;
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
	
	//check command status
	if(pIntReg->CED && pIntReg->ERR)
		return MS_MSPRO_ERROR_FLASH_WRITE;
	
	return MS_MSPRO_NO_ERROR;
}
#endif //MS_WRITE_PATTERN_1

#ifdef MS_WRITE_PATTERN_2
//pattern 2
int ms_write_page(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr, unsigned char * data_buf)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	unsigned char buf[4] = {0,0,0,0};

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	   (ms_mspro_buf->ms.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x06;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}	
	}   	  
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{
		if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	   (ms_mspro_buf->ms.reg_set.write_size != 0x0A))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x0A;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
#endif
	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		packet.param.out.count = 0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		packet.param.out.count = 0x0A;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = block_addr & 0xFF;
	ms_mspro_buf->ms.regs.CMD_Parameter_Reg = 0x20;
	ms_mspro_buf->ms.regs.Page_Address_Reg = page_addr;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.write_addr = 0x10+0x06;
		ms_mspro_buf->ms.reg_set.write_size = 0x0A-0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
		
		packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
		packet.param.out.count = 0x0A-0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.out.buffer = &ms_mspro_buf->ms.regs.Overwrite_Flag_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif

	packet.TPC_cmd.value = TPC_MS_WRITE_PAGE_DATA;      //WRITE_PAGE_DATA
	packet.param.out.count = MS_PAGE_SIZE;          //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	packet.param.out.buffer = data_buf;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_WRITE;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_mspro_wait_int(ms_mspro_info, &packet);
	if(error)
		return error;
		
	//get INT register
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
	}
	else
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
		packet.param.in.count = 1;
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
		
	//check command status
	pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
	if(pIntReg->CMDNK)                  //BLOCK_READ CMD can not be excuted
		return MS_MSPRO_ERROR_CMDNK;
	if(pIntReg->CED && pIntReg->ERR)
		return MS_MSPRO_ERROR_FLASH_WRITE;

	return MS_MSPRO_NO_ERROR;
}
#endif //MS_WRITE_PATTERN_2

int ms_copy_page(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long source_block_addr, unsigned char source_page_addr, unsigned long dest_block_addr, unsigned char dest_page_addr, unsigned char * data_buf)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_Status_Register1_t * pStatusReg1;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.read_addr != 0x01) ||
	   	  (ms_mspro_buf->ms.reg_set.read_size != 0x10) ||
	   	  (ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	  (ms_mspro_buf->ms.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.read_addr = 0x01;
			ms_mspro_buf->ms.reg_set.read_size = 0x10;
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x06;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}	   			
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.read_addr != 0x01) ||
	   	  (ms_mspro_buf->ms.reg_set.read_size != 0x19) ||
	   	  (ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	  (ms_mspro_buf->ms.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.read_addr = 0x01;
			ms_mspro_buf->ms.reg_set.read_size = 0x19;
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x06;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
#endif

	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
	packet.param.out.count = 6;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (source_block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (source_block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = source_block_addr & 0xFF;
	ms_mspro_buf->ms.regs.CMD_Parameter_Reg = 0x20;
	ms_mspro_buf->ms.regs.Page_Address_Reg = source_page_addr;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_READ;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_mspro_wait_int(ms_mspro_info, &packet);
	if(error)
		return error;
	
	packet.TPC_cmd.value = TPC_MS_MSPRO_READ_REG;       //READ_REG
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		packet.param.in.count = 0x10;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		packet.param.in.count = 0x19;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
	packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.read_addr = 0x01+0x10;
		ms_mspro_buf->ms.reg_set.read_size = 0x19-0x10;
		ms_mspro_buf->ms.reg_set.write_addr = 0x10;
		ms_mspro_buf->ms.reg_set.write_size = 0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
		return error;
		
		packet.TPC_cmd.value = TPC_MS_MSPRO_READ_REG;       //READ_REG
		packet.param.in.count = 0x19-0x10;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.Block_Address_Reg2;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif

	//get INT register
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
	}

	//check command status
	pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
	if(pIntReg->CMDNK)                  //BLOCK_READ CMD can not be excuted
		return MS_MSPRO_ERROR_CMDNK;

	if(!(pIntReg->CED & pIntReg->BREQ))
		return MS_MSPRO_ERROR_FLASH_READ;
		
	if(pIntReg->ERR)
	{
		packet.TPC_cmd.value = TPC_MS_READ_PAGE_DATA;       //READ_PAGE_DATA
		packet.param.in.count = MS_PAGE_SIZE;           //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.in.buffer = data_buf;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;

		pStatusReg1 = (MS_Status_Register1_t*)&ms_mspro_buf->ms.regs.Status_Reg1;
		if(pStatusReg1->UCDT | pStatusReg1->UCEX | pStatusReg1->UCFG)
			return MS_MSPRO_ERROR_FLASH_READ;
			
		packet.TPC_cmd.value = TPC_MS_WRITE_PAGE_DATA;      //WRITE_PAGE_DATA
		packet.param.out.count = MS_PAGE_SIZE;          //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.out.buffer = data_buf;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
	packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	ms_mspro_buf->ms.reg_set.write_addr = 0x10;
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		ms_mspro_buf->ms.reg_set.write_size = 0x06;
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		ms_mspro_buf->ms.reg_set.write_size = 0x0A;
#endif
	packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		packet.param.out.count = 0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		packet.param.out.count = 0x0A;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (dest_block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (dest_block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = dest_block_addr & 0xFF;
	ms_mspro_buf->ms.regs.CMD_Parameter_Reg = 0x20;
	ms_mspro_buf->ms.regs.Page_Address_Reg = dest_page_addr;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{	
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.write_addr = 0x10+0x06;
		ms_mspro_buf->ms.reg_set.write_size = 0x0A-0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
		
		packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
		packet.param.out.count = 0x0A-0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.out.buffer = &ms_mspro_buf->ms.regs.Overwrite_Flag_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_WRITE;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_mspro_wait_int(ms_mspro_info, &packet);
	if(error)
		return error;
		
	//get INT register
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
	}
	else
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
		packet.param.in.count = 1;
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}

	//check command status
	pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
	if(pIntReg->CMDNK)                  //BLOCK_READ CMD can not be excuted
		return MS_MSPRO_ERROR_CMDNK;
	if(pIntReg->CED && pIntReg->ERR)
		return MS_MSPRO_ERROR_FLASH_WRITE;
		
	return MS_MSPRO_NO_ERROR;
}

int ms_read_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr, unsigned short page_nums, unsigned char * data_buf)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	unsigned long data_offset = 0;
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;
		
	if(page_nums == 0)
		return MS_MSPRO_ERROR_PARAMETER;

	if((ms_mspro_buf->ms.reg_set.read_addr != 0x16) ||
	   (ms_mspro_buf->ms.reg_set.read_size != 0x08) ||
	   (ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   (ms_mspro_buf->ms.reg_set.write_size != 0x06))
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.read_addr = 0x16;
		ms_mspro_buf->ms.reg_set.read_size = 0x08;
		ms_mspro_buf->ms.reg_set.write_addr = 0x10;
		ms_mspro_buf->ms.reg_set.write_size = 0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}

	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
	packet.param.out.count = 6;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = block_addr & 0xFF;
	ms_mspro_buf->ms.regs.CMD_Parameter_Reg = 0x00;
	ms_mspro_buf->ms.regs.Page_Address_Reg = page_addr;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_READ;
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
			ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
		}
		else
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
			packet.param.in.count = 1;
			packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
		
		//check command status
		pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
		if(pIntReg->CMDNK)
		{                   //BLOCK_READ CMD can not be excuted
			return MS_MSPRO_ERROR_CMDNK;
		}
		if(pIntReg->CED & pIntReg->ERR & pIntReg->BREQ)
		{
			error = MS_MSPRO_ERROR_FLASH_READ;
			break;
		}
		else if(pIntReg->CED & pIntReg->BREQ)
		{
			error = MS_MSPRO_NO_ERROR;
			break;
		}
		
		page_nums--;
		
		if(page_nums > 0)
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_READ_REG;       //READ_REG
			packet.param.in.count = 0x08;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			packet.param.in.buffer = &ms_mspro_buf->ms.regs.Overwrite_Flag_Reg;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;

			packet.TPC_cmd.value = TPC_MS_READ_PAGE_DATA;       //READ_PAGE_DATA
			packet.param.in.count = MS_PAGE_SIZE;           //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			packet.param.in.buffer = data_buf+data_offset;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;

#ifdef AMLOGIC_CHIP_SUPPORT
			data_offset += ((unsigned long)data_buf == 0x3400000) ? 0 : MS_PAGE_SIZE;
#else
			data_offset += MS_PAGE_SIZE;
#endif
		}
		else    //page_nums == 0
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
			packet.param.out.count = 1;
			packet.param.out.buffer = buf;
			packet.param.out.buffer[0] = CMD_MS_BLOCK_END;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}

	if(error == MS_MSPRO_NO_ERROR)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_READ_REG;       //READ_REG
		packet.param.in.count = 0x08;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.Overwrite_Flag_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;

		packet.TPC_cmd.value = TPC_MS_READ_PAGE_DATA;       //READ_PAGE_DATA
		packet.param.in.count = MS_PAGE_SIZE;           //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.in.buffer = data_buf+data_offset;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
		
	return MS_MSPRO_NO_ERROR;
}

int ms_write_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr, unsigned short page_nums, unsigned char * data_buf)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	unsigned long data_offset = 0;
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;
		
	if(page_nums == 0)
		return MS_MSPRO_ERROR_PARAMETER;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	  	   (ms_mspro_buf->ms.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x06;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}	  	   
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	   (ms_mspro_buf->ms.reg_set.write_size != 0x0A))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x0A;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
#endif

	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		packet.param.out.count = 0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		packet.param.out.count = 0x0A;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = block_addr & 0xFF;
	ms_mspro_buf->ms.regs.CMD_Parameter_Reg = 0x00;
	ms_mspro_buf->ms.regs.Page_Address_Reg = page_addr;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.write_addr = 0x10+0x06;
		ms_mspro_buf->ms.reg_set.write_size = 0x0A-0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
		
		packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
		packet.param.out.count = 0x0A-0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.out.buffer = &ms_mspro_buf->ms.regs.Overwrite_Flag_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_WRITE;
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
			ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
		}
		else
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
			packet.param.in.count = 1;
			packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
		
		//check command status
		pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
		if(pIntReg->CMDNK)                  //BLOCK_READ CMD can not be excuted
			return MS_MSPRO_ERROR_CMDNK;
		if(pIntReg->CED & pIntReg->ERR)
			return MS_MSPRO_ERROR_FLASH_WRITE;
		else if(pIntReg->CED)
			break;
		
		if(page_nums > 0)
		{
			page_nums--;

			packet.TPC_cmd.value = TPC_MS_WRITE_PAGE_DATA;      //WRITE_PAGE_DATA
			packet.param.out.count = MS_PAGE_SIZE;          //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			packet.param.out.buffer = data_buf+data_offset;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;

			data_offset += MS_PAGE_SIZE;
		}
		else    //page_nums == 0
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
			packet.param.out.count = 1;
			packet.param.out.buffer = buf;
			packet.param.out.buffer[0] = CMD_MS_BLOCK_END;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
		
	return MS_MSPRO_NO_ERROR;
}

int ms_erase_block(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;
		
	if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   (ms_mspro_buf->ms.reg_set.write_size != 0x04))
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.write_addr = 0x10;
		ms_mspro_buf->ms.reg_set.write_size = 0x04;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}

	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
	packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = block_addr & 0xFF;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_ERASE;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_mspro_wait_int(ms_mspro_info, &packet);
	if(error)
		return error;
		
	//get INT register
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
	}
	else
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
		packet.param.in.count = 1;
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
		
	//check command status
	pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
	if(pIntReg->CMDNK)                  //BLOCK_READ CMD can not be excuted
		return MS_MSPRO_ERROR_CMDNK;
	if(pIntReg->CED & pIntReg->ERR)
		return MS_MSPRO_ERROR_FLASH_ERASE;
	else if(!pIntReg->CED)
		return MS_MSPRO_ERROR_CED;

	return MS_MSPRO_NO_ERROR;
}

int ms_read_extra_data(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_Status_Register1_t * pStatusReg1;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.read_addr != 0x01) ||
	   	   (ms_mspro_buf->ms.reg_set.read_size != 0x10) ||
	   	   (ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	   (ms_mspro_buf->ms.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.read_addr = 0x01;
			ms_mspro_buf->ms.reg_set.read_size = 0x10;
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x06;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}	   	  
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.read_addr != 0x01) ||
	   	   (ms_mspro_buf->ms.reg_set.read_size != 0x19) ||
	   	   (ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	   (ms_mspro_buf->ms.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.read_addr = 0x01;
			ms_mspro_buf->ms.reg_set.read_size = 0x19;
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x06;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
#endif

	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
	packet.param.out.count = 6;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = block_addr & 0xFF;
	ms_mspro_buf->ms.regs.CMD_Parameter_Reg = 0x40;
	ms_mspro_buf->ms.regs.Page_Address_Reg = page_addr;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_READ;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_mspro_wait_int(ms_mspro_info, &packet);
	if(error)
		return error;
	
	packet.TPC_cmd.value = TPC_MS_MSPRO_READ_REG;       //READ_REG
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		packet.param.in.count = 0x10;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		packet.param.in.count = 0x19;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
	packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.read_addr = 0x01+0x10;
		ms_mspro_buf->ms.reg_set.read_size = 0x19-0x10;
		ms_mspro_buf->ms.reg_set.write_addr = 0x10;
		ms_mspro_buf->ms.reg_set.write_size = 0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
		
		packet.TPC_cmd.value = TPC_MS_MSPRO_READ_REG;       //READ_REG
		packet.param.in.count = 0x19-0x10;               //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.Block_Address_Reg2;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif
		
	//get INT register
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
	}

	//check command status
	pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
	if(pIntReg->CMDNK)                  //BLOCK_READ CMD can not be excuted
		return MS_MSPRO_ERROR_CMDNK;

	pStatusReg1 = (MS_Status_Register1_t*)&ms_mspro_buf->ms.regs.Status_Reg1;
	if((pIntReg->CED & pIntReg->ERR) &&
	   (pStatusReg1->UCEX | pStatusReg1->UCFG))
		return MS_MSPRO_ERROR_FLASH_READ;
	
	return MS_MSPRO_NO_ERROR;
}

int ms_write_extra_data(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	  	   (ms_mspro_buf->ms.reg_set.write_size != 0x06))
	  	{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.write_size = 0x06;

			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	   (ms_mspro_buf->ms.reg_set.write_size != 0x0A))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x0A;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
#endif 	

	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		packet.param.out.count = 0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		packet.param.out.count = 0x0A;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = block_addr & 0xFF;
	ms_mspro_buf->ms.regs.CMD_Parameter_Reg = 0x40;
	ms_mspro_buf->ms.regs.Page_Address_Reg = page_addr;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.write_addr = 0x10+0x06;
		ms_mspro_buf->ms.reg_set.write_size = 0x0A-0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
		
		packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
		packet.param.out.count = 0x0A-0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.out.buffer = &ms_mspro_buf->ms.regs.Overwrite_Flag_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_WRITE;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_mspro_wait_int(ms_mspro_info, &packet);
	if(error)
		return error;
		
	//get INT register
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
	}
	else
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
		packet.param.in.count = 1;
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
		
	//check command status
	pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
	if(pIntReg->CMDNK)                  //BLOCK_READ CMD can not be excuted
		return MS_MSPRO_ERROR_CMDNK;
	if(pIntReg->CED && pIntReg->ERR)
		return MS_MSPRO_ERROR_FLASH_WRITE;
	else if(!pIntReg->CED)
		return MS_MSPRO_ERROR_CED;

	return MS_MSPRO_NO_ERROR;
}

int ms_overwrite_extra_data(MS_MSPRO_Card_Info_t *ms_mspro_info, unsigned long block_addr, unsigned char page_addr, unsigned char mask_data)
{
	MS_MSPRO_TPC_Packet_t packet;
	MS_MSPRO_INT_Register_t * pIntReg;
	MS_MSPRO_Card_Buffer_t *ms_mspro_buf = (MS_MSPRO_Card_Buffer_t *)(ms_mspro_info->ms_mspro_buf);
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	   (ms_mspro_buf->ms.reg_set.write_size != 0x06))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x06;

			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}	   	
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
	{	
		if((ms_mspro_buf->ms.reg_set.write_addr != 0x10) ||
	   	   (ms_mspro_buf->ms.reg_set.write_size != 0x07))
		{
			packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
			packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
			ms_mspro_buf->ms.reg_set.write_addr = 0x10;
			ms_mspro_buf->ms.reg_set.write_size = 0x07;
			packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
			error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
			if(error)
				return error;
		}
	}
#endif

	packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
		packet.param.out.count = 0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
#ifdef MS_MSPRO_SW_CONTROL
	if(MS_WORK_MODE == CARD_SW_MODE)
		packet.param.out.count = 0x07;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
#endif
	ms_mspro_buf->ms.regs.System_Parameter_Reg = (ms_mspro_info->interface_mode==INTERFACE_PARALLEL) ? 0x88 : 0x80;
	ms_mspro_buf->ms.regs.Block_Address_Reg2 = (block_addr >> 16) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg1 = (block_addr >> 8) & 0xFF;
	ms_mspro_buf->ms.regs.Block_Address_Reg0 = block_addr & 0xFF;
	ms_mspro_buf->ms.regs.CMD_Parameter_Reg = 0x80;
	ms_mspro_buf->ms.regs.Page_Address_Reg = page_addr;
	ms_mspro_buf->ms.regs.Overwrite_Flag_Reg = mask_data;
	packet.param.out.buffer = &ms_mspro_buf->ms.regs.System_Parameter_Reg;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

#ifdef MS_MSPRO_HW_CONTROL
	if(MS_WORK_MODE == CARD_HW_MODE)
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_SET_RW_REG_ADRS;    //WRITE_REG: Status, Type, Catagory, Class
		packet.param.out.count = 4;             //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		ms_mspro_buf->ms.reg_set.write_addr = 0x10+0x06;
		ms_mspro_buf->ms.reg_set.write_size = 0x07-0x06;
		packet.param.out.buffer = (unsigned char *)&ms_mspro_buf->ms.reg_set;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
		
		packet.TPC_cmd.value = TPC_MS_MSPRO_WRITE_REG;      //WRITE_REG
		packet.param.out.count = 0x07-0x06;              //READ_ADRS,READ_SIZE,WRITE_ADRS,WRITE_SIZE
		packet.param.out.buffer = &ms_mspro_buf->ms.regs.Overwrite_Flag_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
#endif

	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_BLOCK_WRITE;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;

	error = ms_mspro_wait_int(ms_mspro_info, &packet);
	if(error)
		return error;
		
	//get INT register
	if(ms_mspro_info->interface_mode == INTERFACE_PARALLEL)
	{
		ms_mspro_buf->ms.regs.INT_Reg = packet.int_reg;
	}
	else
	{
		packet.TPC_cmd.value = TPC_MS_MSPRO_GET_INT;        //SET_CMD
		packet.param.in.count = 1;
		packet.param.in.buffer = &ms_mspro_buf->ms.regs.INT_Reg;
		error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
		if(error)
			return error;
	}
		
	//check command status
	pIntReg = (MS_MSPRO_INT_Register_t*)&ms_mspro_buf->ms.regs.INT_Reg;
	if(pIntReg->CMDNK)                  //BLOCK_READ CMD can not be excuted
		return MS_MSPRO_ERROR_CMDNK;
	if(pIntReg->CED && pIntReg->ERR)
		return MS_MSPRO_ERROR_FLASH_WRITE;
	else if(!pIntReg->CED)
		return MS_MSPRO_ERROR_CED;

	return MS_MSPRO_NO_ERROR;
}

int ms_sleep(void)
{
	return 0;
}

int ms_clear_buffer(void)
{
	return 0;
}

int ms_flash_stop(void)
{
	return 0;
}

int ms_reset(MS_MSPRO_Card_Info_t *ms_mspro_info)
{
	MS_MSPRO_TPC_Packet_t packet;
	
	int error;
	
	unsigned char* buf = ms_mspro_info->data_buf;
	
	packet.TPC_cmd.value = TPC_MS_MSPRO_SET_CMD;        //SET_CMD
	packet.param.out.count = 1;
	packet.param.out.buffer = buf;
	packet.param.out.buffer[0] = CMD_MS_RESET;
	error = ms_mspro_packet_communicate(ms_mspro_info, &packet);
	if(error)
		return error;
	return 0;
}
