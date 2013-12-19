#ifndef TARGET_PARAMS_H
#define TARGET_PARAMS_H

struct bcm_target_params {
	u32 m_u32CfgVersion;
	u32 m_u32CenterFrequency;
	u32 m_u32BandAScan;
	u32 m_u32BandBScan;
	u32 m_u32BandCScan;
	u32 m_u32ErtpsOptions;
	u32 m_u32PHSEnable;
	u32 m_u32HoEnable;
	u32 m_u32HoReserved1;
	u32 m_u32HoReserved2;
	u32 m_u32MimoEnable;
	u32 m_u32SecurityEnable;
	u32 m_u32PowerSavingModesEnable; /* bit 1: 1 Idlemode enable; bit2: 1 Sleepmode Enable */
	/* PowerSaving Mode Options:
	 * bit 0 = 1: CPE mode - to keep pcmcia if alive;
	 * bit 1 = 1: CINR reporting in Idlemode Msg
	 * bit 2 = 1: Default PSC Enable in sleepmode
	 */
	u32 m_u32PowerSavingModeOptions;
	u32 m_u32ArqEnable;
	/* From Version #3, the HARQ section renamed as general */
	u32 m_u32HarqEnable;
	u32 m_u32EEPROMFlag;
	/* BINARY TYPE - 4th MSByte: Interface Type -  3rd MSByte: Vendor Type - 2nd MSByte
	 * Unused - LSByte
	 */
	u32 m_u32Customize;
	u32 m_u32ConfigBW;  /* In Hz */
	u32 m_u32ShutDownInitThresholdTimer;
	u32 m_u32RadioParameter;
	u32 m_u32PhyParameter1;
	u32 m_u32PhyParameter2;
	u32 m_u32PhyParameter3;
	u32 m_u32TestOptions; /* in eval mode only; lower 16bits = basic cid for testing; then bit 16 is test cqich,bit 17  test init rang; bit 18 test periodic rang and bit 19 is test harq ack/nack */
	u32 m_u32MaxMACDataperDLFrame;
	u32 m_u32MaxMACDataperULFrame;
	u32 m_u32Corr2MacFlags;
	u32 HostDrvrConfig1;
	u32 HostDrvrConfig2;
	u32 HostDrvrConfig3;
	u32 HostDrvrConfig4;
	u32 HostDrvrConfig5;
	u32 HostDrvrConfig6;
	u32 m_u32SegmentedPUSCenable;
	/* removed SHUT down related 'unused' params from here to sync 4.x and 5.x CFG files..
	 * BAMC Related Parameters
	 * Bit 0-15 Band AMC signaling configuration: Bit 1 = 1  Enable Band AMC signaling.
	 * bit 16-31 Band AMC Data configuration: Bit 16 = 1  Band AMC 2x3 support.
	 */
	u32 m_u32BandAMCEnable;
};

#endif
