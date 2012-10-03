#ifndef TARGET_PARAMS_H
#define TARGET_PARAMS_H

typedef struct _TARGET_PARAMS
{
      B_UINT32 m_u32CfgVersion;

      // Scanning Related Params
      B_UINT32 m_u32CenterFrequency;
      B_UINT32 m_u32BandAScan;
      B_UINT32 m_u32BandBScan;
      B_UINT32 m_u32BandCScan;


      // QoS Params
      B_UINT32 m_u32ErtpsOptions;

      B_UINT32 m_u32PHSEnable;


      // HO Params
      B_UINT32 m_u32HoEnable;

      B_UINT32 m_u32HoReserved1;
      B_UINT32 m_u32HoReserved2;
      // Power Control Params

      B_UINT32 m_u32MimoEnable;

      B_UINT32 m_u32SecurityEnable;

      B_UINT32 m_u32PowerSavingModesEnable; //bit 1: 1 Idlemode enable; bit2: 1 Sleepmode Enable
	  /* PowerSaving Mode Options:
	     bit 0 = 1: CPE mode - to keep pcmcia if alive;
	     bit 1 = 1: CINR reporting in Idlemode Msg
	     bit 2 = 1: Default PSC Enable in sleepmode*/
      B_UINT32 m_u32PowerSavingModeOptions;

      B_UINT32 m_u32ArqEnable;

      // From Version #3, the HARQ section renamed as general
      B_UINT32 m_u32HarqEnable;
       // EEPROM Param Location
       B_UINT32  m_u32EEPROMFlag;
       // BINARY TYPE - 4th MSByte: Interface Type -  3rd MSByte: Vendor Type - 2nd MSByte
       // Unused - LSByte
      B_UINT32   m_u32Customize;
      B_UINT32   m_u32ConfigBW;  /* In Hz */
      B_UINT32   m_u32ShutDownInitThresholdTimer;

      B_UINT32  m_u32RadioParameter;
      B_UINT32  m_u32PhyParameter1;
      B_UINT32  m_u32PhyParameter2;
      B_UINT32  m_u32PhyParameter3;

      B_UINT32	  m_u32TestOptions; // in eval mode only; lower 16bits = basic cid for testing; then bit 16 is test cqich,bit 17  test init rang; bit 18 test periodic rang and bit 19 is test harq ack/nack

	B_UINT32 m_u32MaxMACDataperDLFrame;
	B_UINT32 m_u32MaxMACDataperULFrame;

	B_UINT32 m_u32Corr2MacFlags;

    //adding driver params.
	B_UINT32 HostDrvrConfig1;
    B_UINT32 HostDrvrConfig2;
    B_UINT32 HostDrvrConfig3;
    B_UINT32 HostDrvrConfig4;
    B_UINT32 HostDrvrConfig5;
    B_UINT32 HostDrvrConfig6;
    B_UINT32 m_u32SegmentedPUSCenable;

	// removed SHUT down related 'unused' params from here to sync 4.x and 5.x CFG files..

    //BAMC Related Parameters
    //Bit 0-15 Band AMC signaling configuration: Bit 1 = 1  Enable Band AMC signaling.
    //bit 16-31 Band AMC Data configuration: Bit 16 = 1  Band AMC 2x3 support.
	B_UINT32 m_u32BandAMCEnable;

} stTargetParams,TARGET_PARAMS,*PTARGET_PARAMS, STARGETPARAMS, *PSTARGETPARAMS;

#endif
