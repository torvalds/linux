/*
 * PKUnity Real-Time Clock (RTC) control registers
 */
/*
 * RTC Alarm Reg RTC_RTAR
 */
#define RTC_RTAR	(PKUNITY_RTC_BASE + 0x0000)
/*
 * RTC Count Reg RTC_RCNR
 */
#define RTC_RCNR	(PKUNITY_RTC_BASE + 0x0004)
/*
 * RTC Trim Reg RTC_RTTR
 */
#define RTC_RTTR	(PKUNITY_RTC_BASE + 0x0008)
/*
 * RTC Status Reg RTC_RTSR
 */
#define RTC_RTSR	(PKUNITY_RTC_BASE + 0x0010)

/*
 * ALarm detected RTC_RTSR_AL
 */
#define RTC_RTSR_AL		FIELD(1, 1, 0)
/*
 * 1 Hz clock detected RTC_RTSR_HZ
 */
#define RTC_RTSR_HZ		FIELD(1, 1, 1)
/*
 * ALarm interrupt Enable RTC_RTSR_ALE
 */
#define RTC_RTSR_ALE		FIELD(1, 1, 2)
/*
 * 1 Hz clock interrupt Enable RTC_RTSR_HZE
 */
#define RTC_RTSR_HZE		FIELD(1, 1, 3)

