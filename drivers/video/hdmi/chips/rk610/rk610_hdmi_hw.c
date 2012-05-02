#include <linux/delay.h>
#include <linux/mfd/rk610_core.h>
#include "rk610_hdmi.h"
#include "rk610_hdmi_hw.h"
static struct rk610_hdmi_hw_inf g_hw_inf;
static EDID_INF g_edid;
static byte edid_buf[EDID_BLOCK_SIZE];
static struct edid_result Rk610_edid_result;
byte DoEdidRead (struct i2c_client *client);
static int RK610_hdmi_soft_reset(struct i2c_client *client);
static int Rk610_hdmi_Display_switch(struct i2c_client *client);
static int Rk610_hdmi_sys_power_up(struct i2c_client *client);
static int Rk610_hdmi_sys_power_down(struct i2c_client *client);

static int Rk610_hdmi_i2c_read_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_recv(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}
static int Rk610_hdmi_i2c_write_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_send(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}
static int RK610_hdmi_audio_mute(struct i2c_client *client,bool enable)
{
	char c;
	int ret=0;
    c = ((~enable)&1)<<1;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0x05, &c);
}
static int Rk610_hdmi_pwr_mode(struct i2c_client *client, int mode)
{
    char c;
    int ret=0;
	RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    switch(mode){
     case NORMAL:
	   	Rk610_hdmi_sys_power_down(client);
		c=0x82;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe3, &c);
		c=0x00;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe5, &c);
        c=0x00;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe4, &c);
	    c=0x00;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe7, &c);
        c=0x8e;
		ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe1, &c);
		c=0x00;
	   	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xce, &c);
		c=0x01;
		ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xce, &c);
		RK610_hdmi_audio_mute(client,1);
		Rk610_hdmi_sys_power_up(client);
		g_hw_inf.analog_sync = 1;
		break;
	case LOWER_PWR:
		RK610_hdmi_audio_mute(client,0);
	   	Rk610_hdmi_sys_power_down(client);
		c=0x02;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe3, &c);
	    c=0x1c;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe5, &c);
	    c=0x8c;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe1, &c);
        c=0x04;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe7, &c);
	    c=0x03;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe4, &c);
		break;
	default:
	    RK610_ERR(&client->dev,"unkown rk610 hdmi pwr mode %d\n",mode);
    }
    return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
int Rk610_hdmi_suspend(struct i2c_client *client)
{
    int ret = 0;
	RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
	g_hw_inf.suspend_flag = 1;
	g_hw_inf.hpd = 0;
	Rk610_hdmi_unplug(client);
	return ret;
}
int Rk610_hdmi_resume(struct i2c_client *client)
{
	int ret = 0;
	char c = 0;
	RK610_DBG(&client->dev, "%s \n",__FUNCTION__);
	Rk610_hdmi_i2c_read_p0_reg(client, 0xc8, &c);
	if(c & RK610_HPD_PLUG ){
        Rk610_hdmi_plug(client);
		g_hw_inf.hpd=1;
	}
	else{
        Rk610_hdmi_unplug(client);
		g_hw_inf.hpd=0;
	}
	g_hw_inf.suspend_flag = 0;
	return ret;
}
#endif
static int Rk610_hdmi_sys_power_up(struct i2c_client *client)
{
    char c = 0;
    int ret = 0;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    c= RK610_SYS_CLK<<2 |RK610_SYS_PWR_ON<<1 |RK610_INT_POL;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0x00, &c);
	return ret;
}
static int Rk610_hdmi_sys_power_down(struct i2c_client *client)
{
    char c = 0;
    int ret = 0;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    c= RK610_SYS_CLK<<2 |RK610_SYS_PWR_OFF<<1 |RK610_INT_POL;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0x00, &c);
	return ret;
}
//X=11.2896M/(4*100k), X = {0x4c,0x4b}
static int RK610_DDC_BUS_CONFIG(struct i2c_client *client)
{
    char c = 0;
    int ret = 0;
    c= RK610_DDC_CONFIG&0xff;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0x4b, &c);
	c= (RK610_DDC_CONFIG>>8)&0xff;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0x4c, &c);
	return ret;
}
static int RK610_read_edid_block(struct i2c_client *client,u8 block, u8 * buf)
{
    char c = 0;
    int ret = 0,i;
    u8 Segment = 0;
	u8 Offset = 0;
    if(block%2)
    Offset = EDID_BLOCK_SIZE;
    if(block/2)
    Segment = 1;
    RK610_DBG(&client->dev,"EDID DATA (Segment = %d Block = %d Offset = %d):\n", (int) Segment, (int) block, (int) Offset);
    //set edid fifo first addr
	c = 0x00;
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x4f, &c);
	//set edid word address 00/80
	c = Offset;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0x4e, &c);
	//set edid segment pointer
	c = Segment;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0x4d, &c);
	
	//enable edid interrupt
	c=0xc6;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xc0, &c);
	//wait edid interrupt
    msleep(10);
	RK610_DBG(&client->dev,"Interrupt generated\n");
	c=0x00;
	ret =Rk610_hdmi_i2c_read_p0_reg(client, 0xc1, &c);
	RK610_DBG(&client->dev,"Interrupt reg=%x \n",c);
	//clear EDID interrupt reg
	c=0x04;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xc1, &c);
	for(i=0; i <EDID_BLOCK_SIZE;i++){
	    c = 0;	    
		Rk610_hdmi_i2c_read_p0_reg(client, 0x50, &c);
		buf[i] = c;
	}
	return ret;
}
#if 0
//------------------------------------------------------------------------------
// Function Name: Parse861ShortDescriptors()
// Function Description: Parse CEA-861 extension short descriptors of the EDID block
//                  passed as a parameter and save them in global structure g_edid.
//
// Accepts: A pointer to the EDID 861 Extension block being parsed.
// Returns: EDID_PARSED_OK if EDID parsed correctly. Error code if failed.
// Globals: EDID data
// NOTE: Fields that are not supported by the 9022/4 (such as deep color) were not parsed.
//------------------------------------------------------------------------------
byte Parse861ShortDescriptors (byte *Data)
{
    byte LongDescriptorOffset;
    byte DataBlockLength;
    byte DataIndex;
    byte ExtendedTagCode;
    byte VSDB_BaseOffset = 0;

    byte V_DescriptorIndex = 0;  // static to support more than one extension
    byte A_DescriptorIndex = 0;  // static to support more than one extension

    byte TagCode;

    byte i;
    byte j;

    if (Data[EDID_TAG_ADDR] != EDID_EXTENSION_TAG)
    {
        RK610_HDMI_ERR("EDID -> Extension Tag Error\n");
        return EDID_EXT_TAG_ERROR;
    }

    if (Data[EDID_REV_ADDR] != EDID_REV_THREE)
    {
        RK610_HDMI_ERR("EDID -> Revision Error\n"));
        return EDID_REV_ADDR_ERROR;
    }

    LongDescriptorOffset = Data[LONG_DESCR_PTR_IDX];    // block offset where long descriptors start

    g_edid.UnderScan = ((Data[MISC_SUPPORT_IDX]) >> 7) & LSBIT;  // byte #3 of CEA extension version 3
    g_edid.BasicAudio = ((Data[MISC_SUPPORT_IDX]) >> 6) & LSBIT;
    g_edid.YCbCr_4_4_4 = ((Data[MISC_SUPPORT_IDX]) >> 5) & LSBIT;
    g_edid.YCbCr_4_2_2 = ((Data[MISC_SUPPORT_IDX]) >> 4) & LSBIT;

    DataIndex = EDID_DATA_START;            // 4

    while (DataIndex < LongDescriptorOffset)
    {
        TagCode = (Data[DataIndex] >> 5) & THREE_LSBITS;
        DataBlockLength = Data[DataIndex++] & FIVE_LSBITS;
        if ((DataIndex + DataBlockLength) > LongDescriptorOffset)
        {
            RK610_HDMI_ERR("EDID -> V Descriptor Overflow\n");
            return EDID_V_DESCR_OVERFLOW;
        }

        i = 0;                                  // num of short video descriptors in current data block

        switch (TagCode)
        {
            case VIDEO_D_BLOCK:
                while ((i < DataBlockLength) && (i < MAX_V_DESCRIPTORS))        // each SVD is 1 byte long
                {
                    g_edid.VideoDescriptor[V_DescriptorIndex++] = Data[DataIndex++];
                    i++;
                }
                DataIndex += DataBlockLength - i;   // if there are more STDs than MAX_V_DESCRIPTORS, skip the last ones. Update DataIndex

                RK610_RK610_DBG(&client->dev,"EDID -> Short Descriptor Video Block\n");
                break;

            case AUDIO_D_BLOCK:
                while (i < DataBlockLength/3)       // each SAD is 3 bytes long
                {
                    j = 0;
                    while (j < AUDIO_DESCR_SIZE)    // 3
                    {
                        g_edid.AudioDescriptor[A_DescriptorIndex][j++] = Data[DataIndex++];
                    }
                    A_DescriptorIndex++;
                    i++;
                }
                RK610_HDMI_DBG("EDID -> Short Descriptor Audio Block\n");
                break;

            case  SPKR_ALLOC_D_BLOCK:
                g_edid.SpkrAlloc[i++] = Data[DataIndex++];       // although 3 bytes are assigned to Speaker Allocation, only
                DataIndex += 2;                                     // the first one carries information, so the next two are ignored by this code.
                RK610_HDMI_DBG("EDID -> Short Descriptor Speaker Allocation Block\n");
                break;

            case USE_EXTENDED_TAG:
                ExtendedTagCode = Data[DataIndex++];

                switch (ExtendedTagCode)
                {
                    case VIDEO_CAPABILITY_D_BLOCK:
                        RK610_HDMI_DBG("EDID -> Short Descriptor Video Capability Block\n");

                        // TO BE ADDED HERE: Save "video capability" parameters in g_edid data structure
                        // Need to modify that structure definition
                        // In the meantime: just increment DataIndex by 1
                        DataIndex += 1;    // replace with reading and saving the proper data per CEA-861 sec. 7.5.6 while incrementing DataIndex
                        break;

                    case COLORIMETRY_D_BLOCK:
                        g_edid.ColorimetrySupportFlags = Data[DataIndex++] & BITS_1_0;
                        g_edid.MetadataProfile = Data[DataIndex++] & BITS_2_1_0;

                        RK610_HDMI_DBG("EDID -> Short Descriptor Colorimetry Block\n");
                        break;
                }
                break;

            case VENDOR_SPEC_D_BLOCK:
                VSDB_BaseOffset = DataIndex - 1;

                if ((Data[DataIndex++] == 0x03) &&    // check if sink is HDMI compatible
                    (Data[DataIndex++] == 0x0C) &&
                    (Data[DataIndex++] == 0x00))

                    g_edid.RK610_HDMI_Sink = TRUE;
                else
                    g_edid.RK610_HDMI_Sink = FALSE;

                g_edid.CEC_A_B = Data[DataIndex++];  // CEC Physical address
                g_edid.CEC_C_D = Data[DataIndex++];

#ifdef DEV_SUPPORT_CEC
				// Take the Address that was passed in the EDID and use this API
				// to set the physical address for CEC.
				{
					word	phyAddr;
					phyAddr = (word)g_edid.CEC_C_D;	 // Low-order nibbles
					phyAddr |= ((word)g_edid.CEC_A_B << 8); // Hi-order nibbles
					// Is the new PA different from the current PA?
					if (phyAddr != SI_CecGetDevicePA ())
					{
						// Yes!  So change the PA
						SI_CecSetDevicePA (phyAddr);
					}
				}
#endif

                if ((DataIndex + 7) > VSDB_BaseOffset + DataBlockLength)        // Offset of 3D_Present bit in VSDB
                        g_edid._3D_Supported = FALSE;
                else if (Data[DataIndex + 7] >> 7)
                        g_edid._3D_Supported = TRUE;
                else
                        g_edid._3D_Supported = FALSE;

                DataIndex += DataBlockLength - RK610_HDMI_SIGNATURE_LEN - CEC_PHYS_ADDR_LEN; // Point to start of next block
                RK610_HDMI_DBG("EDID -> Short Descriptor Vendor Block\n");
                break;

            default:
                RK610_HDMI_DBG("EDID -> Unknown Tag Code\n");
                return EDID_UNKNOWN_TAG_CODE;

        }                   // End, Switch statement
    }                       // End, while (DataIndex < LongDescriptorOffset) statement

    return EDID_SHORT_DESCRIPTORS_OK;
}

//------------------------------------------------------------------------------
// Function Name: Parse861LongDescriptors()
// Function Description: Parse CEA-861 extension long descriptors of the EDID block
//                  passed as a parameter and printf() them to the screen.
//
// Accepts: A pointer to the EDID block being parsed
// Returns: An error code if no long descriptors found; EDID_PARSED_OK if descriptors found.
// Globals: none
//------------------------------------------------------------------------------
byte Parse861LongDescriptors (byte *Data)
{
    byte LongDescriptorsOffset;
    byte DescriptorNum = 1;

    LongDescriptorsOffset = Data[LONG_DESCR_PTR_IDX];   // EDID block offset 2 holds the offset

    if (!LongDescriptorsOffset)                         // per CEA-861-D, table 27
    {
        TPI_DEBUG_PRINT(("EDID -> No Detailed Descriptors\n"));
        return EDID_NO_DETAILED_DESCRIPTORS;
    }

    // of the 1st 18-byte descriptor
    while (LongDescriptorsOffset + LONG_DESCR_LEN < EDID_BLOCK_SIZE)
    {
        TPI_EDID_PRINT(("Parse Results - CEA-861 Long Descriptor #%d:\n", (int) DescriptorNum));
        TPI_EDID_PRINT(("===============================================================\n"));

#if (CONF__TPI_EDID_PRINT == ENABLE)
        if (!ParseDetailedTiming(Data, LongDescriptorsOffset, EDID_BLOCK_2_3))
                        break;
#endif
        LongDescriptorsOffset +=  LONG_DESCR_LEN;
        DescriptorNum++;
    }

    return EDID_LONG_DESCRIPTORS_OK;
}

//------------------------------------------------------------------------------
// Function Name: Parse861Extensions()
// Function Description: Parse CEA-861 extensions from EDID ROM (EDID blocks beyond
//                  block #0). Save short descriptors in global structure
//                  g_edid. printf() long descriptors to the screen.
//
// Accepts: The number of extensions in the EDID being parsed
// Returns: EDID_PARSED_OK if EDID parsed correctly. Error code if failed.
// Globals: EDID data
// NOTE: Fields that are not supported by the 9022/4 (such as deep color) were not parsed.
//------------------------------------------------------------------------------
byte Parse861Extensions (struct i2c_client *client,byte NumOfExtensions)
{
	byte i,j,k;
	
	byte ErrCode;
	
//	byte V_DescriptorIndex = 0;
//	byte A_DescriptorIndex = 0;
	
    byte Block = 0;
	
	g_edid.HDMI_Sink = FALSE;

	do
	{
	    Block++;
	    HDMI_DBG("\n");
	    
	    for (j=0, i=0; j<128; j++)
	    {
	        k = edid_buf[j];
	        HDMI_DBG("%2.2X ", (int) k);
	        i++;
	
	        if (i == 0x10)
	        {
	            HDMI_DBG("\n");
	            i = 0;
	        }
	    }
	    HDMI_DBG("\n");
	    RK610_read_edid_block(client,Block, edid_buf);
	    if ((NumOfExtensions > 1) && (Block == 1))
	    {
	        continue;
	    }
	
	    ErrCode = Parse861ShortDescriptors(edid_buf);
	    if (ErrCode != EDID_SHORT_DESCRIPTORS_OK)
	    {
	        return ErrCode;
	    }
	
	    ErrCode = Parse861LongDescriptors(edid_buf);
	    if (ErrCode != EDID_LONG_DESCRIPTORS_OK)
	    {
	        return ErrCode;
	    }
	
	} while (Block < NumOfExtensions);

	return EDID_OK;
}

//------------------------------------------------------------------------------
// Function Name: ParseEDID()
// Function Description: Extract sink properties from its EDID file and save them in
//                  global structure g_edid.
//
// Accepts: none
// Returns: TRUE or FLASE
// Globals: EDID data
// NOTE: Fields that are not supported by the 9022/4 (such as deep color) were not parsed.
//------------------------------------------------------------------------------
static byte ParseEDID (byte *pEdid, byte *numExt)
{
	if (!CheckEDID_Header(pEdid))
	{
		// first 8 bytes of EDID must be {0, FF, FF, FF, FF, FF, FF, 0}
		HDMI_ERR("EDID -> Incorrect Header\n");
		return EDID_INCORRECT_HEADER;
	}

	if (!DoEDID_Checksum(pEdid))
	{
		// non-zero EDID checksum
		HDMI_ERR("EDID -> Checksum Error\n");
		return EDID_CHECKSUM_ERROR;
	}

	*numExt = pEdid[NUM_OF_EXTEN_ADDR];	// read # of extensions from offset 0x7E of block 0
	HDMI_DBG("EDID -> 861 Extensions = %d\n", (int) *numExt);

	if (!(*numExt))
	{
		// No extensions to worry about
		HDMI_DBG("EDID -> EDID_NO_861_EXTENSIONS\n");
		return EDID_NO_861_EXTENSIONS;
	}
	return EDID_OK;
}
#endif

//------------------------------------------------------------------------------
// Function Name: CheckEDID_Header()
// Function Description: Checks if EDID header is correct per VESA E-EDID standard
//
// Accepts: Pointer to 1st EDID block
// Returns: TRUE or FLASE
// Globals: EDID data
//------------------------------------------------------------------------------
byte CheckEDID_Header (byte *Block)
{
	byte i = 0;

	if (Block[i])               // byte 0 must be 0
    	return FALSE;

	for (i = 1; i < 1 + EDID_HDR_NO_OF_FF; i++)
	{
    	if(Block[i] != 0xFF)    // bytes [1..6] must be 0xFF
        	return FALSE;
	}

	if (Block[i])               // byte 7 must be 0
    	return FALSE;

	return TRUE;
}

//------------------------------------------------------------------------------
// Function Name: DoEDID_Checksum()
// Function Description: Calculte checksum of the 128 byte block pointed to by the
//                  pointer passed as parameter
//
// Accepts: Pointer to a 128 byte block whose checksum needs to be calculated
// Returns: TRUE or FLASE
// Globals: EDID data
//------------------------------------------------------------------------------
byte DoEDID_Checksum (byte *Block)
{
	byte i;
	byte CheckSum = 0;

	for (i = 0; i < EDID_BLOCK_SIZE; i++)
    	CheckSum += Block[i];

	if (CheckSum)
    	return FALSE;

	return TRUE;
}
//------------------------------------------------------------------------------
// Function Name: Parse861ShortDescriptors()
// Function Description: Parse CEA-861 extension short descriptors of the EDID block
//                  passed as a parameter and save them in global structure g_edid.
//
// Accepts: A pointer to the EDID 861 Extension block being parsed.
// Returns: EDID_PARSED_OK if EDID parsed correctly. Error code if failed.
// Globals: EDID data
// NOTE: Fields that are not supported by the 9022/4 (such as deep color) were not parsed.
//------------------------------------------------------------------------------
byte Parse861ShortDescriptors (struct i2c_client *client,byte *Data)
{
    byte LongDescriptorOffset;
    byte DataBlockLength;
    byte DataIndex;
    byte ExtendedTagCode;
    byte VSDB_BaseOffset = 0;

    byte V_DescriptorIndex = 0;  // static to support more than one extension
    byte A_DescriptorIndex = 0;  // static to support more than one extension

    byte TagCode;

    byte i;
    byte j;

    if (Data[EDID_TAG_ADDR] != EDID_EXTENSION_TAG)
    {
        RK610_ERR(&client->dev,"EDID -> Extension Tag Error\n");
        return EDID_EXT_TAG_ERROR;
    }

    if (Data[EDID_REV_ADDR] != EDID_REV_THREE)
    {
        RK610_ERR(&client->dev,"EDID -> Revision Error\n");
        return EDID_REV_ADDR_ERROR;
    }

    LongDescriptorOffset = Data[LONG_DESCR_PTR_IDX];    // block offset where long descriptors start

    g_edid.UnderScan = ((Data[MISC_SUPPORT_IDX]) >> 7) & LSBIT;  // byte #3 of CEA extension version 3
    g_edid.BasicAudio = ((Data[MISC_SUPPORT_IDX]) >> 6) & LSBIT;
    g_edid.YCbCr_4_4_4 = ((Data[MISC_SUPPORT_IDX]) >> 5) & LSBIT;
    g_edid.YCbCr_4_2_2 = ((Data[MISC_SUPPORT_IDX]) >> 4) & LSBIT;

    DataIndex = EDID_DATA_START;            // 4

    while (DataIndex < LongDescriptorOffset)
    {
        TagCode = (Data[DataIndex] >> 5) & THREE_LSBITS;
        DataBlockLength = Data[DataIndex++] & FIVE_LSBITS;
        if ((DataIndex + DataBlockLength) > LongDescriptorOffset)
        {
            RK610_ERR(&client->dev,"EDID -> V Descriptor Overflow\n");
            return EDID_V_DESCR_OVERFLOW;
        }

        i = 0;                                  // num of short video descriptors in current data block

        switch (TagCode)
        {
            case VIDEO_D_BLOCK:
                while ((i < DataBlockLength) && (i < MAX_V_DESCRIPTORS))        // each SVD is 1 byte long
                {
                    g_edid.VideoDescriptor[V_DescriptorIndex++] = Data[DataIndex++];
                    i++;
                }
                DataIndex += DataBlockLength - i;   // if there are more STDs than MAX_V_DESCRIPTORS, skip the last ones. Update DataIndex

                RK610_DBG(&client->dev,"EDID -> Short Descriptor Video Block\n");
                break;

            case AUDIO_D_BLOCK:
                while (i < DataBlockLength/3)       // each SAD is 3 bytes long
                {
                    j = 0;
                    while (j < AUDIO_DESCR_SIZE)    // 3
                    {
                        g_edid.AudioDescriptor[A_DescriptorIndex][j++] = Data[DataIndex++];
                    }
                    A_DescriptorIndex++;
                    i++;
                }
                RK610_DBG(&client->dev,"EDID -> Short Descriptor Audio Block\n");
                break;

            case  SPKR_ALLOC_D_BLOCK:
                g_edid.SpkrAlloc[i++] = Data[DataIndex++];       // although 3 bytes are assigned to Speaker Allocation, only
                DataIndex += 2;                                     // the first one carries information, so the next two are ignored by this code.
                RK610_DBG(&client->dev,"EDID -> Short Descriptor Speaker Allocation Block\n");
                break;

            case USE_EXTENDED_TAG:
                ExtendedTagCode = Data[DataIndex++];

                switch (ExtendedTagCode)
                {
                    case VIDEO_CAPABILITY_D_BLOCK:
                        RK610_DBG(&client->dev,"EDID -> Short Descriptor Video Capability Block\n");

                        // TO BE ADDED HERE: Save "video capability" parameters in g_edid data structure
                        // Need to modify that structure definition
                        // In the meantime: just increment DataIndex by 1
                        DataIndex += 1;    // replace with reading and saving the proper data per CEA-861 sec. 7.5.6 while incrementing DataIndex
                        break;

                    case COLORIMETRY_D_BLOCK:
                        g_edid.ColorimetrySupportFlags = Data[DataIndex++] & BITS_1_0;
                        g_edid.MetadataProfile = Data[DataIndex++] & BITS_2_1_0;

                        RK610_DBG(&client->dev,"EDID -> Short Descriptor Colorimetry Block\n");
                        break;
                }
                break;

            case VENDOR_SPEC_D_BLOCK:
                VSDB_BaseOffset = DataIndex - 1;

                if ((Data[DataIndex++] == 0x03) &&    // check if sink is HDMI compatible
                    (Data[DataIndex++] == 0x0C) &&
                    (Data[DataIndex++] == 0x00))

                    g_edid.HDMI_Sink = TRUE;
                else
                    g_edid.HDMI_Sink = FALSE;

                g_edid.CEC_A_B = Data[DataIndex++];  // CEC Physical address
                g_edid.CEC_C_D = Data[DataIndex++];

#ifdef DEV_SUPPORT_CEC
				// Take the Address that was passed in the EDID and use this API
				// to set the physical address for CEC.
				{
					word	phyAddr;
					phyAddr = (word)g_edid.CEC_C_D;	 // Low-order nibbles
					phyAddr |= ((word)g_edid.CEC_A_B << 8); // Hi-order nibbles
					// Is the new PA different from the current PA?
					if (phyAddr != SI_CecGetDevicePA ())
					{
						// Yes!  So change the PA
						SI_CecSetDevicePA (phyAddr);
					}
				}
#endif

                if ((DataIndex + 7) > VSDB_BaseOffset + DataBlockLength)        // Offset of 3D_Present bit in VSDB
                        g_edid._3D_Supported = FALSE;
                else if (Data[DataIndex + 7] >> 7)
                        g_edid._3D_Supported = TRUE;
                else
                        g_edid._3D_Supported = FALSE;

                DataIndex += DataBlockLength - HDMI_SIGNATURE_LEN - CEC_PHYS_ADDR_LEN; // Point to start of next block
                RK610_DBG(&client->dev,"EDID -> Short Descriptor Vendor Block\n");
                break;

            default:
                RK610_ERR(&client->dev,"EDID -> Unknown Tag Code\n");
                return EDID_UNKNOWN_TAG_CODE;

        }                   // End, Switch statement
    }                       // End, while (DataIndex < LongDescriptorOffset) statement

    return EDID_SHORT_DESCRIPTORS_OK;
}
//------------------------------------------------------------------------------
// Function Name: ParseEDID()
// Function Description: Extract sink properties from its EDID file and save them in
//                  global structure g_edid.
//
// Accepts: none
// Returns: TRUE or FLASE
// Globals: EDID data
// NOTE: Fields that are not supported by the 9022/4 (such as deep color) were not parsed.
//------------------------------------------------------------------------------
static byte ParseEDID (struct i2c_client *client,byte *pEdid, byte *numExt)
{
	if (!CheckEDID_Header(pEdid))
	{
		// first 8 bytes of EDID must be {0, FF, FF, FF, FF, FF, FF, 0}
		RK610_ERR(&client->dev,"EDID -> Incorrect Header\n");
		return EDID_INCORRECT_HEADER;
	}

	if (!DoEDID_Checksum(pEdid))
	{
		// non-zero EDID checksum
		RK610_ERR(&client->dev,"EDID -> Checksum Error\n");
		return EDID_CHECKSUM_ERROR;
	}

	*numExt = pEdid[NUM_OF_EXTEN_ADDR];	// read # of extensions from offset 0x7E of block 0
	RK610_DBG(&client->dev,"EDID -> 861 Extensions = %d\n", (int) *numExt);

	if (!(*numExt))
	{
		// No extensions to worry about
		RK610_DBG(&client->dev,"EDID -> EDID_NO_861_EXTENSIONS\n");
		return EDID_NO_861_EXTENSIONS;
	}
	return EDID_OK;
}

int Rk610_Parse_resolution(void)
{
    int i,vic;
    memset(&Rk610_edid_result,0,sizeof(struct edid_result));
    for(i=0;i < MAX_V_DESCRIPTORS;i++){
        vic = g_edid.VideoDescriptor[i]&0x7f;
        if(vic == HDMI_VIC_1080p_50Hz)
            Rk610_edid_result.supported_1080p_50Hz = 1;
        else if(vic == HDMI_VIC_1080p_60Hz)
            Rk610_edid_result.supported_1080p_60Hz = 1; 
        else if(vic == HDMI_VIC_720p_50Hz)
            Rk610_edid_result.supported_720p_50Hz = 1; 
        else if(vic == HDMI_VIC_720p_60Hz)
            Rk610_edid_result.supported_720p_60Hz = 1; 
        else if(vic == HDMI_VIC_576p_50Hz)
            Rk610_edid_result.supported_576p_50Hz = 1; 
        else if(vic == HDMI_VIC_480p_60Hz)
            Rk610_edid_result.supported_720x480p_60Hz = 1; 
    }
    #ifdef  RK610_DEBUG
    printk("rk610_hdmi:1080p_50Hz %s\n",Rk610_edid_result.supported_1080p_50Hz?"support":"not support");
    printk("rk610_hdmi:1080p_60Hz %s\n",Rk610_edid_result.supported_1080p_60Hz?"support":"not support");
    printk("rk610_hdmi:720p_50Hz %s\n",Rk610_edid_result.supported_720p_50Hz?"support":"not support");
    printk("rk610_hdmi:720p_60Hz %s\n",Rk610_edid_result.supported_720p_60Hz?"support":"not support");
    printk("rk610_hdmi:576p_50Hz %s\n",Rk610_edid_result.supported_576p_50Hz?"support":"not support");
    printk("rk610_hdmi:720x480p_60Hz %s\n",Rk610_edid_result.supported_720x480p_60Hz?"support":"not support");
    #endif
    return 0;
}

int Rk610_Get_Optimal_resolution(int resolution_set)
{
	int resolution_real;
	int find_resolution = 0;
	
    Rk610_Parse_resolution();
	switch(resolution_set){
	case HDMI_1280x720p_50Hz:
		if(Rk610_edid_result.supported_720p_50Hz){
			resolution_real = HDMI_1280x720p_50Hz;
			find_resolution = 1;
		}
		break;
	case HDMI_1280x720p_60Hz:
		if(Rk610_edid_result.supported_720p_60Hz){
			resolution_real = HDMI_1280x720p_60Hz;
			find_resolution = 1;
		}
		break;
	case HDMI_720x576p_50Hz_4x3:
		if(Rk610_edid_result.supported_576p_50Hz){
			resolution_real = HDMI_720x576p_50Hz_4x3;
			find_resolution = 1;
		}
		break;
	case HDMI_720x576p_50Hz_16x9:
		if(Rk610_edid_result.supported_576p_50Hz){
			resolution_real = HDMI_720x576p_50Hz_16x9;
			find_resolution = 1;
		}
		break;
	case HDMI_720x480p_60Hz_4x3:
		if(Rk610_edid_result.supported_720x480p_60Hz){
			resolution_real = HDMI_720x480p_60Hz_4x3;
			find_resolution = 1;
		}
		break;
	case HDMI_720x480p_60Hz_16x9:
		if(Rk610_edid_result.supported_720x480p_60Hz){
			resolution_real = HDMI_720x480p_60Hz_16x9;
			find_resolution = 1;
		}
		break;
	case HDMI_1920x1080p_50Hz:
		if(Rk610_edid_result.supported_1080p_50Hz){
			resolution_real = HDMI_1920x1080p_50Hz;
			find_resolution = 1;
		}
		break;
	case HDMI_1920x1080p_60Hz:
		if(Rk610_edid_result.supported_1080p_60Hz){
			resolution_real = HDMI_1920x1080p_60Hz;
			find_resolution = 1;
		}
		break;
	default:
		break;
	}

	if(find_resolution == 0){

		if(Rk610_edid_result.supported_720p_50Hz)
			resolution_real = HDMI_1280x720p_50Hz;
		else if(Rk610_edid_result.supported_720p_60Hz)
			resolution_real = HDMI_1280x720p_60Hz;
		else if(Rk610_edid_result.supported_1080p_50Hz)
			resolution_real = HDMI_1920x1080p_50Hz;
		else if(Rk610_edid_result.supported_1080p_60Hz)
			resolution_real = HDMI_1920x1080p_60Hz;
		else if(Rk610_edid_result.supported_576p_50Hz)
			resolution_real = HDMI_720x576p_50Hz_4x3;
		else if(Rk610_edid_result.supported_720x480p_60Hz)
			resolution_real = HDMI_720x480p_60Hz_4x3;
		else
			resolution_real = HDMI_1280x720p_60Hz;
	}

	return resolution_real;
}

byte DoEdidRead (struct i2c_client *client)
{
    u8 NumOfExtensions=0;
    u8 Result;
    u8 i,j;
	// If we already have valid EDID data, ship this whole thing
	if (g_edid.edidDataValid == FALSE)
	{
	    Rk610_hdmi_sys_power_up(client);
		// Request access to DDC bus from the receiver
        RK610_DDC_BUS_CONFIG(client);
        memset(edid_buf,0,EDID_BLOCK_SIZE);
		RK610_read_edid_block(client,EDID_BLOCK0, edid_buf);		// read first 128 bytes of EDID ROM
        RK610_DBG(&client->dev,"/************first block*******/\n");
        #ifdef  RK610_DEBUG
	    for (j=0; j<EDID_BLOCK_SIZE; j++)
	        {
	            if(j%16==0)
	                printk("\n%x :",j);
	                printk("%2.2x ", edid_buf[j]);
	        }
	    #endif
        Result = ParseEDID(client,edid_buf, &NumOfExtensions);
        if(Result!=EDID_OK){
            if(Result==EDID_NO_861_EXTENSIONS){
                g_edid.HDMI_Sink = FALSE;
            }
            else {
                g_edid.HDMI_Sink = FALSE;
                return FALSE;
            }
        }
	    else{
	    	NumOfExtensions = edid_buf[NUM_OF_EXTEN_ADDR];
	        for(i=1;i<=NumOfExtensions;i++){
	        RK610_DBG(&client->dev,"\n/************block %d*******/\n",i);
	        memset(edid_buf,0,EDID_BLOCK_SIZE);
            RK610_read_edid_block(client,i, edid_buf); 
            Parse861ShortDescriptors(client,edid_buf);
        #ifdef  RK610_DEBUG
            for (j=0; j<EDID_BLOCK_SIZE; j++){
	            if(j%16==0)
	            printk("\n%x :",j);
	            printk("%2.2X ", edid_buf[j]);
	            }
	    #endif
	        }

            g_edid.HDMI_Sink = TRUE;
	    }

#if 0
		Result = ParseEDID(edid_buf, &NumOfExtensions);
			if (Result != EDID_OK)
			{
				if (Result == EDID_NO_861_EXTENSIONS)
				{
					RK610_DBG(&client->dev,"EDID -> No 861 Extensions\n");
					g_edid.HDMI_Sink = FALSE;
					g_edid.YCbCr_4_4_4 = FALSE;
					g_edid.YCbCr_4_2_2 = FALSE;
					g_edid.CEC_A_B = 0x00;
					g_edid.CEC_C_D = 0x00;
				}
				else
				{
					RK610_DBG(&client->dev,"EDID -> Parse FAILED\n");
					g_edid.HDMI_Sink = TRUE;
					g_edid.YCbCr_4_4_4 = FALSE;
					g_edid.YCbCr_4_2_2 = FALSE;
					g_edid.CEC_A_B = 0x00;
					g_edid.CEC_C_D = 0x00;
				}
			}
			else
			{
				RK610_DBG(&client->dev,"EDID -> Parse OK\n");
				Result = Parse861Extensions(NumOfExtensions);		// Parse 861 Extensions (short and long descriptors);
				if (Result != EDID_OK)
				{
					RK610_DBG(&client->dev,"EDID -> Extension Parse FAILED\n");
					g_edid.HDMI_Sink = FALSE;
					g_edid.YCbCr_4_4_4 = FALSE;
					g_edid.YCbCr_4_2_2 = FALSE;
					g_edid.CEC_A_B = 0x00;
					g_edid.CEC_C_D = 0x00;
				}
				else
				{
					RK610_DBG(&client->dev,"EDID -> Extension Parse OK\n");
					g_edid.HDMI_Sink = TRUE;
				}
			}
#endif
		RK610_DBG(&client->dev,"EDID -> NumOfExtensions = %d\n", NumOfExtensions);
		RK610_DBG(&client->dev,"EDID -> g_edid.HDMI_Sink = %d\n", (int)g_edid.HDMI_Sink);
		//RK610_DBG(&client->dev,"EDID -> g_edid.YCbCr_4_4_4 = %d\n", (int)g_edid.YCbCr_4_4_4);
		//RK610_DBG(&client->dev,"EDID -> g_edid.YCbCr_4_2_2 = %d\n", (int)g_edid.YCbCr_4_2_2);
		//RK610_DBG(&client->dev,"EDID -> g_edid.CEC_A_B = 0x%x\n", (int)g_edid.CEC_A_B);
		//RK610_DBG(&client->dev,"EDID -> g_edid.CEC_C_D = 0x%x\n", (int)g_edid.CEC_C_D);

		g_edid.edidDataValid = TRUE;
	}
	return TRUE;
}

static int Rk610_hdmi_Display_switch(struct i2c_client *client)
{
    char c;
    int ret=0;
    int mode;
    mode = (g_edid.HDMI_Sink == TRUE)? DISPLAY_HDMI:DISPLAY_DVI;
    ret = Rk610_hdmi_i2c_read_p0_reg(client, 0x52, &c);
    c &= ((~(1<<1))| mode<<1);
    ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x52, &c);
    RK610_DBG(&client->dev,">>>%s mode=%d,c=%x",__func__,mode,c);
    return ret;
}

static int Rk610_hdmi_Config_audio_informat(struct i2c_client *client)
{
    char c;
    int ret=0;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    //Select configure for Audio Info
    c=0x08;
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x9f, &c);
	//Configure the Audio info to HDMI RX.
	c=0x84;     //HB0
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa0, &c);
	c=0x01;     //HB1
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa1, &c);
	c=0x0a;     //HB2
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa2, &c);
	//c=0x00;   //PB0
	//ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa3, &c);
	c=0x11;     //PB1
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa4, &c);
	c=0x09;     //PB2
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa5, &c);
	c=0x00;     //PB3
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa6, &c);
	c=0x00;     //PB4
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa7, &c);
	c=0x01;     //PB5
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa8, &c);
    return ret;
}

static int Rk610_hdmi_Config_Avi_informat(struct i2c_client *client ,u8 vic)
{
    char c;
    int ret=0;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    //Select configure for AVI Info
    c = 0x06;   
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x9f, &c);

	//Configure the AVI info to HDMI RX
	c = 0x82;   //HB0
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa0, &c);
	c = 0x02;   //HB1
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa1, &c);
	c = 0x0d;   //HB2
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa2, &c);
	//c=0x00;   //PB0
	//ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa3, &c);
	c = 0x00;   //PB1
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa4, &c);
	c = 0x08;   //PB2
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa5, &c);
	c = 0x70;   //PB3
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa6, &c);
	c = vic;    //PB4
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa7, &c);
	c = 0x40;   //PB5
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xa8, &c);
    return ret;
}
static int Rk610_hdmi_Config_Video(struct i2c_client *client, u8 video_format)
{
    char vic;
    int ret = 0;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    switch(video_format){
		case HDMI_720x480p_60Hz_4x3:
		case HDMI_720x480p_60Hz_16x9:
			vic = HDMI_VIC_480p_60Hz;
			break;
        case HDMI_720x576p_50Hz_4x3:
        case HDMI_720x576p_50Hz_16x9:
            vic = HDMI_VIC_576p_50Hz;
            break;
		case HDMI_1280x720p_50Hz:
		    vic = HDMI_VIC_720p_50Hz;
			break;
		case HDMI_1280x720p_60Hz:
			vic = HDMI_VIC_720p_60Hz;
			break;
		case HDMI_1920x1080p_50Hz:
		    vic = HDMI_VIC_1080p_50Hz;
			break;
		case HDMI_1920x1080p_60Hz:
			vic = HDMI_VIC_1080p_60Hz;
			break;
		default:
			vic = 0x04;
			break;
		}
    ret = Rk610_hdmi_Config_Avi_informat(client,vic);
    return ret;
}
static int Rk610_hdmi_Config_Audio(struct i2c_client *client ,u8 audio_fs)
{
    char c=0;
    int ret = 0;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    c=0x01;
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x35, &c);
	c=0x3c;
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x38, &c);
	c=0x00;
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x39, &c);
    c=0x18;
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x40, &c);
	switch(audio_fs){
        case HDMI_I2S_Fs_44100:
	        c=0x80;
	        ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x41, &c);
	        break;
        case HDMI_I2S_Fs_48000:
            c=0x92;
	        ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x41, &c);
	        break;
        default:
	        c=0x80;
		    ret = Rk610_hdmi_i2c_write_p0_reg(client, 0x41, &c);
            break;
	}
	Rk610_hdmi_Config_audio_informat(client);
	return ret;
}

int Rk610_hdmi_Set_Video(u8 video_format)
{
    if(g_hw_inf.video_format !=video_format){
    g_hw_inf.video_format = video_format;
    g_hw_inf.config_param |= VIDEO_CHANGE;
    }
    return 0;
}
int Rk610_hdmi_Set_Audio(u8 audio_fs)
{
    if(g_hw_inf.audio_fs !=audio_fs){
    g_hw_inf.audio_fs = audio_fs;
    g_hw_inf.config_param |= AUDIO_CHANGE;
    }
    return 0;
}
static int RK610_hdmi_Driver_mode(struct i2c_client *client)
{
    char c;
    int ret=0;
    c=0x8e;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe1, &c);
	c=0x04;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xe2, &c);
	return 0;
}
static int RK610_hdmi_PLL_mode(struct i2c_client *client)
{
    char c;
    int ret=0;
    c=0x10;
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xe8, &c);
	c=0x2c;
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xe6, &c);
	c=0x00;
	ret = Rk610_hdmi_i2c_write_p0_reg(client, 0xe5, &c);
	return 0;
}
void Rk610_hdmi_plug(struct i2c_client *client)
{
    RK610_DBG(&client->dev,">>> hdmi plug \n");
	DoEdidRead(client);
	Rk610_hdmi_Display_switch(client);
	Rk610_hdmi_pwr_mode(client,NORMAL);
}
void Rk610_hdmi_unplug(struct i2c_client *client)
{
    RK610_DBG(&client->dev,">>> hdmi unplug \n");
	g_edid.edidDataValid = FALSE;
	Rk610_hdmi_pwr_mode(client,LOWER_PWR); 
}
void Rk610_hdmi_event_work(struct i2c_client *client, bool *hpd)
{
	char c=0;
	int ret=0;
    if(g_hw_inf.suspend_flag == 1){
        *hpd = 0;
        return ;
    }

	c=0x00;
	ret =Rk610_hdmi_i2c_read_p0_reg(client, 0xc1, &c);
	if(c & RK610_HPD_EVENT){
		RK610_DBG(&client->dev,">>>HPD EVENT\n");
		/**********clear hpd event******/
		c = RK610_HPD_EVENT;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xc1, &c);
	    ret =Rk610_hdmi_i2c_read_p0_reg(client, 0xc8, &c);
		if(c & RK610_HPD_PLUG ){
        //    Rk610_hdmi_plug(client);
			g_hw_inf.hpd=1;
		}
		else{
          //  Rk610_hdmi_unplug(client);
			g_hw_inf.hpd=0;
		}

	}
	if(c & RK610_EDID_EVENT){
			/**********clear hpd event******/
		c = RK610_EDID_EVENT;
	    ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xc1, &c);
		RK610_DBG(&client->dev,">>>EDID EVENT\n");
		/*************clear edid event*********/
	}
	*hpd = g_hw_inf.hpd;
}
int Rk610_hdmi_Config_Done(struct i2c_client *client)
{
    char c;
    int ret=0;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);

    ret =Rk610_hdmi_sys_power_down(client);

    if(g_hw_inf.config_param != 0){
	c=0x08;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0x04, &c);
	c=0x01;
	ret =Rk610_hdmi_i2c_write_p0_reg(client, 0x01, &c);
    if(g_hw_inf.config_param & VIDEO_CHANGE){
        Rk610_hdmi_Config_Video(client,g_hw_inf.video_format);
        g_hw_inf.config_param &= (~VIDEO_CHANGE); 
    }
	if(g_hw_inf.config_param & AUDIO_CHANGE){
        Rk610_hdmi_Config_Audio(client,g_hw_inf.audio_fs);
        g_hw_inf.config_param &= (~AUDIO_CHANGE); 
  	}
    }
    ret =Rk610_hdmi_sys_power_up(client);
    ret =Rk610_hdmi_sys_power_down(client);
    ret =Rk610_hdmi_sys_power_up(client);
	if(g_hw_inf.analog_sync){
		c=0x00;
		ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xce, &c);
		c=0x01;
		ret =Rk610_hdmi_i2c_write_p0_reg(client, 0xce, &c);
		g_hw_inf.analog_sync = 0;
	}

    return ret;
}
#if 0
int Rk610_hdmi_hpd(struct i2c_client *client)
{
    char c;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
	if(Rk610_hdmi_i2c_read_p0_reg(client, 0xc8, &c)<0){
        RK610_ERR(">>>%s I2c trans err",__FUNCTION__);
        return -1;
	}
	if()
	return (c & RK610_HPD_PLUG)?1:0;
}
#endif
static int RK610_hdmi_soft_reset(struct i2c_client *client)
{
    char c;
    int ret;
    //soft reset
    c=0x00;
	ret =Rk610_hdmi_i2c_read_p0_reg(client, 0xce, &c);
	msleep(10);
	c=0x01;
	ret =Rk610_hdmi_i2c_read_p0_reg(client, 0xce, &c);	
	msleep(100);
	return ret;
}
static void Rk610_hdmi_Variable_Initial(void)
{
    memset(&g_hw_inf,0,sizeof(struct rk610_hdmi_hw_inf));
    g_edid.edidDataValid = FALSE;
    g_hw_inf.edid_inf = &g_edid;    
    g_hw_inf.audio_fs = HDMI_I2S_DEFAULT_Fs;
    g_hw_inf.video_format = HDMI_DEFAULT_RESOLUTION;
    g_hw_inf.config_param = AUDIO_CHANGE | VIDEO_CHANGE;
    g_hw_inf.hpd = 0;
    g_hw_inf.suspend_flag = 0;
	g_hw_inf.analog_sync = 0;
}
int Rk610_hdmi_init(struct i2c_client *client)
{
    char c;
    RK610_DBG(&client->dev,"%s \n",__FUNCTION__);
    Rk610_hdmi_Variable_Initial();
    RK610_hdmi_soft_reset(client);
    RK610_hdmi_Driver_mode(client);
    RK610_hdmi_PLL_mode(client);
	Rk610_hdmi_Set_Video(g_hw_inf.video_format);
	Rk610_hdmi_Set_Audio(g_hw_inf.audio_fs);
    Rk610_hdmi_Config_Done(client);
    Rk610_hdmi_i2c_read_p0_reg(client, 0xc8, &c);
	if(c & RK610_HPD_PLUG ){
        Rk610_hdmi_plug(client);
		g_hw_inf.hpd=1;
	}else{
       	Rk610_hdmi_unplug(client);
		g_hw_inf.hpd=0;
	}
	return 0;
}
