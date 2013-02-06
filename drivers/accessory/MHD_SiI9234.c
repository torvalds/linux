/*
 * MHD_SiI9234.c - Driver for Silicon Image MHD SiI9234 Transmitter driver
 *
 * Copyright 2010  Philju Lee (Daniel Lee)
 *
 * Based on preview driver from Silicon Image.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

static int MYDRV_MAJOR;

static int MHDDRV_MAJOR;

static bool tclkStable;
static bool mobileHdCableConnected;
static bool hdmiCableConnected;
static bool dsRxPoweredUp;
static byte tmdsPoweredUp;
static byte txPowerState;
static bool checkTclkStable;


/*===========================================================================*/

static void InitCBusRegs(void);
static byte ReadIndexedRegister(byte PageNum, byte Offset);
void MHD_HW_Reset(void);
void MHD_HW_Off(void);
void MHD_OUT_EN(void);
void MHD_INT_clear(void);

static void I2C_WriteByte(byte deviceID, byte offset, byte value);
static byte I2C_ReadByte(byte deviceID, byte offset);
static byte ReadByteTPI(byte Offset);
static void WriteByteTPI(byte Offset, byte Data);
static void ReadModifyWriteTPI(byte Offset, byte Mask, byte Data);
static void WriteIndexedRegister(byte PageNum, byte Offset, byte Data);
static void sii9234_initializeStateVariables(void);

static void sii9234_enable_interrupts(void);
static byte ReadByteCBUS(byte Offset);
static void WriteByteCBUS(byte Offset, byte Data);
static byte ReadIndexedRegister(byte PageNum, byte Offset);
static void ReadModifyWriteIndexedRegister(byte PageNum, byte Offset,
					byte Mask, byte Data);
static void TxPowerStateD3(void);
static void TxPowerStateD0(void);
static void CheckTxFifoStable(void);
static void HotPlugService(void);
static void ReadModifyWriteCBUS(byte Offset, byte Mask, byte Value);
static void OnMHDCableConnected(void) ;
static void OnDownstreamRxPoweredDown(void);
static void OnHdmiCableDisconnected(void);
static void OnDownstreamRxPoweredUp(void);
static void OnHdmiCableConnected(void);

void sii9234_tpi_init(void);
static void sii9234_register_init(void);
static void mhd_tx_fifo_stable(void);
int MHD_Read_deviceID(void);

/*===========================================================================*/


bool delay_ms(int msec)
{
	mdelay(msec);
	return 0;
}

void MHD_HW_Reset(void)
{
	pr_info("[SIMG]MHD_HW_Reset == Start ==\n");
	SII9234_HW_Reset(SII9234_i2c_client);
	pr_info("[SIMG] MHD_HW_Reset == End ==\n");
}
EXPORT_SYMBOL(MHD_HW_Reset);

void MHD_HW_Off(void)
{
	SII9234_HW_Off(SII9234_i2c_client);
#ifdef	CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE
	mhl_hpd_handler(false);
#endif
}
EXPORT_SYMBOL(MHD_HW_Off);

int MHD_HW_IsOn(void)
{
	return SII9234_HW_IsOn();
}
EXPORT_SYMBOL(MHD_HW_IsOn);

void MHD_OUT_EN(void)
{
	byte state , int_stat;
	int_stat = ReadIndexedRegister(INDEXED_PAGE_0, 0x74);
	pr_info("[HDMI]MHD_OUT_EN INT register value is: 0x%02x\n", int_stat);
	state = ReadIndexedRegister(INDEXED_PAGE_0, 0x81);
	pr_info("[HDMI]MHD_OUT_EN register 0x81 value is: 0x%02x\n", state);

	if ((state & 0x02) && (int_stat & 0x01)) {
		pr_info("[HDMI]MHD_OUT_EN :: enable output\n");
		ReadModifyWriteIndexedRegister(INDEXED_PAGE_0, 0x80, SI_BIT_4,
			0x0);
		msleep(20);
		ReadModifyWriteIndexedRegister(INDEXED_PAGE_0, 0x80, SI_BIT_4,
			SI_BIT_4);
		msleep(60);
		/* set mhd power active mode */
		ReadModifyWriteTPI(TPI_DEVICE_POWER_STATE_CTRL_REG,
				TX_POWER_STATE_MASK, 0x00);

		mhd_tx_fifo_stable();  /*fifo clear*/
	}
	MHD_INT_clear();
}
EXPORT_SYMBOL(MHD_OUT_EN);

void MHD_INT_clear(void)
{
	byte Int_state;
	Int_state = ReadIndexedRegister(INDEXED_PAGE_0, 0x74);
	WriteIndexedRegister(INDEXED_PAGE_0, 0x74, Int_state);
}
EXPORT_SYMBOL(MHD_INT_clear);

void I2C_WriteByte(byte deviceID, byte offset, byte value)
{
	if (deviceID == 0x72)
		SII9234_i2c_write(SII9234_i2c_client, offset, value);
	else if (deviceID == 0x7A)
		SII9234_i2c_write(SII9234A_i2c_client, offset, value);
	else if (deviceID == 0x92)
		SII9234_i2c_write(SII9234B_i2c_client, offset, value);
	else if (deviceID == 0xC8)
		SII9234_i2c_write(SII9234C_i2c_client, offset, value);
	else
		pr_err("[MHL]I2C_WriteByte error %x\n", deviceID);
}

byte I2C_ReadByte(byte deviceID, byte offset)
{
	byte number = 0;
	/*pr_err("[MHL]I2C_ReadByte called ID%x Offset%x\n",deviceID,offset);*/
	if (deviceID == 0x72)
		number = SII9234_i2c_read(SII9234_i2c_client, offset);
	else if (deviceID == 0x7A)
		number = SII9234_i2c_read(SII9234A_i2c_client, offset);
	else if (deviceID == 0x92)
		number = SII9234_i2c_read(SII9234B_i2c_client, offset);
	else if (deviceID == 0xC8)
		number = SII9234_i2c_read(SII9234C_i2c_client, offset);
	else
		pr_err("[MHL]I2C_ReadByte error %x\n", deviceID);
	/*pr_err("[MHL]I2C_ReadByte ID:%x Offset:%x data:%x\n",
		deviceID,offset,number); */
	return number;
}

byte ReadByteTPI(byte Offset)
{
	return I2C_ReadByte(TPI_SLAVE_ADDR, Offset);
}

void WriteByteTPI(byte Offset, byte Data)
{
	I2C_WriteByte(TPI_SLAVE_ADDR, Offset, Data);
}

void ReadModifyWriteTPI(byte Offset, byte Mask, byte Data)
{

	byte Temp;

	Temp = ReadByteTPI(Offset);
	/* Read the current value of the register.*/
	Temp &= ~Mask;
	/*Clear the bits that are set in Mask.*/
	Temp |= (Data & Mask);
	/*OR in new value. Apply Mask to Value for safety.*/
	WriteByteTPI(Offset, Temp);
	/*Write new value back to register.*/
}


void WriteIndexedRegister(byte PageNum, byte Offset, byte Data)
{
	WriteByteTPI(TPI_INDEXED_PAGE_REG, PageNum);	/*Indexed page*/
	WriteByteTPI(TPI_INDEXED_OFFSET_REG, Offset);	/*Indexed register*/
	WriteByteTPI(TPI_INDEXED_VALUE_REG, Data);	/*Write value*/
}


static void sii9234_initializeStateVariables(void)
{

	tclkStable = FALSE;
	checkTclkStable = TRUE;
	tmdsPoweredUp = FALSE;
	mobileHdCableConnected = FALSE;
	hdmiCableConnected = FALSE;
	dsRxPoweredUp = FALSE;
}

static void InitCBusRegs(void)
{
	I2C_WriteByte(0xC8, 0x1F, 0x02);
	/*Heartbeat Max Fail Enable*/
	I2C_WriteByte(0xC8, 0x07, DDC_XLTN_TIMEOUT_MAX_VAL | 0x06);
	/*Increase DDC translation layer timer*/
	I2C_WriteByte(0xC8, 0x40, 0x03);
	/*CBUS Drive Strength*/
	I2C_WriteByte(0xC8, 0x42, 0x06);
	/*CBUS DDC interface ignore segment pointer*/
	I2C_WriteByte(0xC8, 0x36, 0x0C);
	/*I2C_WriteByte(0xC8, 0x44, 0x02);*/
	I2C_WriteByte(0xC8, 0x3D, 0xFD);
	I2C_WriteByte(0xC8, 0x1C, 0x00);
	I2C_WriteByte(0xC8, 0x44, 0x00);
	/*I2C_WriteByte(0xC8, 0x09, 0x60);*/
	/* Enable PVC Xfer aborted / follower aborted*/
}

void sii9234_enable_interrupts(void)
{
	ReadModifyWriteTPI(TPI_INTERRUPT_ENABLE_REG, HOT_PLUG_EVENT_MASK,
		HOT_PLUG_EVENT_MASK);
	WriteIndexedRegister(INDEXED_PAGE_0, 0x75, SI_BIT_5);	/* Enable */
}

static byte ReadByteCBUS(byte Offset)
{
	return I2C_ReadByte(CBUS_SLAVE_ADDR, Offset);
}


static void WriteByteCBUS(byte Offset, byte Data)
{
	I2C_WriteByte(CBUS_SLAVE_ADDR, Offset, Data);
}

static byte ReadIndexedRegister(byte PageNum, byte Offset)
{
	WriteByteTPI(TPI_INDEXED_PAGE_REG, PageNum);	/* Indexed page */
	WriteByteTPI(TPI_INDEXED_OFFSET_REG, Offset);	/* Indexed register */
	return ReadByteTPI(TPI_INDEXED_VALUE_REG);	/* Return read value */
}

static void ReadModifyWriteIndexedRegister(byte PageNum, byte Offset,
					byte Mask, byte Data)
{
	byte Temp;

	/* Read the current value of the register. */
	Temp = ReadIndexedRegister(PageNum, Offset);
	/* Clear the bits that are set in Mask. */
	Temp &= ~Mask;
	/* OR in new value. Apply Mask to Value for safety. */
	Temp |= (Data & Mask);
	/* Write new value back to register. */
	WriteByteTPI(TPI_INDEXED_VALUE_REG, Temp);
}

static void TxPowerStateD3(void)
{

	ReadModifyWriteIndexedRegister(INDEXED_PAGE_1, 0x3D, SI_BIT_0, 0x00);
	pr_info("[SIMG] TX Power State D3\n");
	txPowerState = TX_POWER_STATE_D3;
}

static void TxPowerStateD0(void)
{
	ReadModifyWriteTPI(TPI_DEVICE_POWER_STATE_CTRL_REG,
		TX_POWER_STATE_MASK, 0x00);
	TPI_DEBUG_PRINT(("[SIMG] TX Power State D0\n"));
	txPowerState = TX_POWER_STATE_D0;
}

static void CheckTxFifoStable(void)
{
	byte bTemp;

	bTemp = ReadIndexedRegister(INDEXED_PAGE_0, 0x3E);
	if ((bTemp & (SI_BIT_7 | SI_BIT_6)) != 0x00) {
		TPI_DEBUG_PRINT(("[SIMG] FIFO Overrun / Underrun\n"));
		/* Assert MHD FIFO Reset */
		WriteIndexedRegister(INDEXED_PAGE_0, 0x05,
				SI_BIT_4 | ASR_VALUE);
		mdelay(1);
		/* Deassert MHD FIFO Reset */
		WriteIndexedRegister(INDEXED_PAGE_0, 0x05, ASR_VALUE);
	}
}

static void HotPlugService(void)
{
	/* disable interrupts */
	ReadModifyWriteTPI(TPI_INTERRUPT_ENABLE_REG,
			RECEIVER_SENSE_EVENT_MASK, 0x00);

	/* enable TMDS */
	pr_info("[SIMG] TMDS -> Enabled\n");
	ReadModifyWriteTPI(TPI_SYSTEM_CONTROL_DATA_REG,
			TMDS_OUTPUT_CONTROL_MASK, TMDS_OUTPUT_CONTROL_ACTIVE);
	tmdsPoweredUp = TRUE;

	TxPowerStateD0();

	/* enable interrupts */
	WriteIndexedRegister(INDEXED_PAGE_0, 0x78, 0x01);

	CheckTxFifoStable();
}

static void ReadModifyWriteCBUS(byte Offset, byte Mask, byte Value)
{
	byte Temp = ReadByteCBUS(Offset);
	Temp &= ~Mask;
	Temp |= (Value & Mask);
	WriteByteCBUS(Offset, Temp);
}

static void OnMHDCableConnected(void)
{

	TPI_DEBUG_PRINT(("[SIMG] MHD Connected\n"));

	if (txPowerState == TX_POWER_STATE_D3) {
		/* start tpi */
		WriteByteTPI(TPI_ENABLE, 0x00);	/* Write "0" to 72:C7 to
						   start HW TPI mode */
		/* enable interrupts */
		WriteIndexedRegister(INDEXED_PAGE_0, 0x78, 0x01);

		TxPowerStateD0();
	}

	mobileHdCableConnected = TRUE;

	WriteIndexedRegister(INDEXED_PAGE_0, 0xA0, 0x10);

	TPI_DEBUG_PRINT(("[SIMG] Setting DDC Burst Mode\n"));
	/* Increase DDC translation layer timer (burst mode) */
	WriteByteCBUS(0x07, DDC_XLTN_TIMEOUT_MAX_VAL | 0x0E);
	WriteByteCBUS(0x47, 0x03);
	WriteByteCBUS(0x21, 0x01); /* Heartbeat Disable */
}

void OnDownstreamRxPoweredDown(void)
{

	TPI_DEBUG_PRINT(("[SIMG] DSRX -> Powered Down\n"));

	dsRxPoweredUp = FALSE;

	/* disable TMDS */
	TPI_DEBUG_PRINT(("[SIMG] TMDS -> Disabled\n"));
	ReadModifyWriteTPI(TPI_SYSTEM_CONTROL_DATA_REG,
			TMDS_OUTPUT_CONTROL_MASK,
			TMDS_OUTPUT_CONTROL_POWER_DOWN);
	tmdsPoweredUp = FALSE;
}

void OnHdmiCableDisconnected(void)
{

	TPI_DEBUG_PRINT(("[SIMG] HDMI Disconnected\n"));

	hdmiCableConnected = FALSE;
	OnDownstreamRxPoweredDown();
}


void sii9234_tpi_init(void)
{
	MHD_HW_Reset();

	pr_info("[HDMI]9234 init ++\n");

	sii9234_register_init();

	/* start tpi */
	WriteByteTPI(TPI_ENABLE, 0x00);	/* Write "0" to 72:C7 to
					   start HW TPI mode */

	/* enable interrupts */
	WriteIndexedRegister(INDEXED_PAGE_0, 0x78, 0x01);

	/* mhd rx connected */
	WriteIndexedRegister(INDEXED_PAGE_0,
			0xA0, 0x10); /* TX termination enable */
	WriteByteCBUS(0x07, DDC_XLTN_TIMEOUT_MAX_VAL |
		0x0E);	/* Increase DDC translation layer timer (burst mode) */
	WriteByteCBUS(0x47, 0x03);
	WriteByteCBUS(0x21, 0x01); /* Heartbeat Disable  */

	/* enable mhd tx */
	ReadModifyWriteTPI(TPI_SYSTEM_CONTROL_DATA_REG,
			TMDS_OUTPUT_CONTROL_MASK, TMDS_OUTPUT_CONTROL_ACTIVE);

	/* set mhd power active mode */
	ReadModifyWriteTPI(TPI_DEVICE_POWER_STATE_CTRL_REG,
			TX_POWER_STATE_MASK, 0x00);

	mhd_tx_fifo_stable();		/*fifo clear*/
#ifdef	CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE
	mhl_hpd_handler(true);
#endif
	pr_info("[HDMI]9234 init --\n");
}
EXPORT_SYMBOL(sii9234_tpi_init);


int MHD_Read_deviceID(void)
{

	byte devID;
	word wID;

	devID = ReadIndexedRegister(0x00, 0x03);
	wID = devID << 8;
	devID = ReadIndexedRegister(0x00, 0x02);
	wID |= devID;

	devID = ReadByteTPI(TPI_DEVICE_ID);

	pr_err("SiI %04X\n", (int) wID);

	if (devID == SiI_DEVICE_ID)
		return TRUE;

	pr_err("Unsupported TX\n");
	return FALSE;

}
EXPORT_SYMBOL(MHD_Read_deviceID);


void mhd_tx_fifo_stable(void)
{
	byte tmp = ReadIndexedRegister(INDEXED_PAGE_0, 0x3E);
	if ((tmp & (SI_BIT_7 | SI_BIT_6)) != 0x00) {
		/* Assert Mobile HD FIFO Reset */
		WriteIndexedRegister(INDEXED_PAGE_0, 0x05,
				SI_BIT_4 | ASR_VALUE);
		mdelay(1);
		/* Deassert Mobile HD FIFO Reset */
		WriteIndexedRegister(INDEXED_PAGE_0, 0x05, ASR_VALUE);
	}
}



static void sii9234_register_init(void)
{
	/*Power Up*/
	I2C_WriteByte(0x7A, 0x3D, 0x3F);	/* Power up CVCC 1.2V core */
	I2C_WriteByte(0x92, 0x11, 0x01);	/* Enable TxPLL Clock*/
	I2C_WriteByte(0x92, 0x12, 0x15);	/* Enable Tx Clock Path & Equalizer*/
	I2C_WriteByte(0x72, 0x08, 0x35);	/* Power Up TMDS Tx Core*/

	I2C_WriteByte(0x92, 0x00, 0x00);	/* SIMG: correcting HW default*/
	I2C_WriteByte(0x92, 0x13, 0x60);	/* SIMG: Set termination value*/
	I2C_WriteByte(0x92, 0x14, 0xF0);	/* SIMG: Change CKDT level*/
	I2C_WriteByte(0x92, 0x4B, 0x06);	/* SIMG: Correcting HW default*/

	/*Analog PLL Control*/
	I2C_WriteByte(0x92, 0x17, 0x07);	/* SIMG: PLL Calrefsel*/
	I2C_WriteByte(0x92, 0x1A, 0x20);	/* VCO Cal*/
	I2C_WriteByte(0x92, 0x22, 0xE0);	/* SIMG: Auto EQ*/
	I2C_WriteByte(0x92, 0x23, 0xC0);	/* SIMG: Auto EQ*/
	I2C_WriteByte(0x92, 0x24, 0xA0);	/* SIMG: Auto EQ*/
	I2C_WriteByte(0x92, 0x25, 0x80);	/* SIMG: Auto EQ*/
	I2C_WriteByte(0x92, 0x26, 0x60);	/* SIMG: Auto EQ*/
	I2C_WriteByte(0x92, 0x27, 0x40);	/* SIMG: Auto EQ*/
	I2C_WriteByte(0x92, 0x28, 0x20);	/* SIMG: Auto EQ*/
	I2C_WriteByte(0x92, 0x29, 0x00);	/* SIMG: Auto EQ*/

	/*I2C_WriteByte(0x92, 0x10, 0xF1);*/
	I2C_WriteByte(0x92, 0x4D, 0x02);	/* SIMG: PLL Mode Value (order is important)*/
	/*I2C_WriteByte(0x92, 0x4D, 0x00);*/
	I2C_WriteByte(0x92, 0x4C, 0xA0);	/* Manual zone control*/

	/*I2C_WriteByte(0x72, 0x80, 0x14);*/	/* Enable Rx PLL Clock Value*/
	I2C_WriteByte(0x72, 0x80, 0x34);

	I2C_WriteByte(0x92, 0x31, 0x0B);	/* SIMG: Rx PLL BW value from I2C BW ~ 4MHz*/
	I2C_WriteByte(0x92, 0x45, 0x06);	/* SIMG: DPLL Mode*/
	I2C_WriteByte(0x72, 0xA0, 0xD0);	/* SIMG: Term mode*/
	I2C_WriteByte(0x72, 0xA1, 0xFC);	/* Disable internal Mobile HD driver*/


	I2C_WriteByte(0x72, 0xA3, 0xEB);	/* SIMG: Output Swing  default EB*/
	I2C_WriteByte(0x72, 0xA6, 0x00);	/* SIMG: Swing Offset*/

	I2C_WriteByte(0x72, 0x2B, 0x01);	/* Enable HDCP Compliance workaround*/

	/*CBUS & Discovery*/
	ReadModifyWriteTPI(0x90, SI_BIT_3 | SI_BIT_2, SI_BIT_3);/* CBUS discovery cycle time for each drive and float = 150us*/

	I2C_WriteByte(0x72, 0x91, 0xE5);	/* Skip RGND detection*/

	I2C_WriteByte(0x72, 0x94, 0x66);	/* 1.8V CBUS VTH & GND threshold*/

	/*set bit 2 and 3, which is Initiator Timeout*/
	I2C_WriteByte(CBUS_SLAVE_ADDR, 0x31, I2C_ReadByte(CBUS_SLAVE_ADDR, 0x31) | 0x0c);

	/*original 3x config*/
	I2C_WriteByte(0x72, 0xA5, 0x80);	/* SIMG: RGND Hysterisis, 3x mode for Beast*/
	I2C_WriteByte(0x72, 0x95, 0x31);	/* RGND & single discovery attempt (RGND blocking)*/
	I2C_WriteByte(0x72, 0x96, 0x22);	/* use 1K and 2K setting*/

	ReadModifyWriteTPI(0x95, SI_BIT_6, SI_BIT_6);		/* Force USB ID switch to open*/

	WriteByteTPI(0x92, 0x46);		/* Force MHD mode*/
	WriteByteTPI(0x93, 0xDC);		/* Disable CBUS pull-up during RGND measurement*/

	ReadModifyWriteTPI(0x79, SI_BIT_1 | SI_BIT_2, 0);        /*daniel test...MHL_INT*/

	mdelay(25);
	ReadModifyWriteTPI(0x95, SI_BIT_6, 0x00);	/* Release USB ID switch*/

	I2C_WriteByte(0x72, 0x90, 0x27);	/* Enable CBUS discovery*/

	InitCBusRegs();

	I2C_WriteByte(0x72, 0x05, ASR_VALUE);	/* Enable Auto soft reset on SCDT = 0*/

	I2C_WriteByte(0x72, 0x0D, 0x1C);	/* HDMI Transcode mode enable*/
}

