#include "../sysfs.h"

/* metering ic types of attribute */

#define IIO_DEV_ATTR_CURRENT_A_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(current_a_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_B_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(current_b_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_C_OFFSET(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(current_c_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VOLT_A_OFFSET(_mode, _show, _store, _addr)      \
	IIO_DEVICE_ATTR(volt_a_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VOLT_B_OFFSET(_mode, _show, _store, _addr)      \
	IIO_DEVICE_ATTR(volt_b_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VOLT_C_OFFSET(_mode, _show, _store, _addr)      \
	IIO_DEVICE_ATTR(volt_c_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_A_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_a_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_B_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_b_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_C_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_c_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_A_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_a_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_B_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_b_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_C_OFFSET(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_c_offset, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_A_GAIN(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(current_a_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_B_GAIN(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(current_b_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_C_GAIN(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(current_c_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_APPARENT_POWER_A_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(apparent_power_a_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_APPARENT_POWER_B_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(apparent_power_b_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_APPARENT_POWER_C_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(apparent_power_c_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_A_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_a_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_B_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_b_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_ACTIVE_POWER_C_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(active_power_c_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_A_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_a_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_B_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_b_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_REACTIVE_POWER_C_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(reactive_power_c_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CURRENT_A(_show, _addr)			\
	IIO_DEVICE_ATTR(current_a, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_CURRENT_B(_show, _addr)			\
	IIO_DEVICE_ATTR(current_b, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_CURRENT_C(_show, _addr)			\
	IIO_DEVICE_ATTR(current_c, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_VOLT_A(_show, _addr)			\
	IIO_DEVICE_ATTR(volt_a, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_VOLT_B(_show, _addr)			\
	IIO_DEVICE_ATTR(volt_b, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_VOLT_C(_show, _addr)			\
	IIO_DEVICE_ATTR(volt_c, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_AENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(aenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_LENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(lenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_RAENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(raenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_LAENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(laenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_VAENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(vaenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_LVAENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(lvaenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_RVAENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(rvaenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_LVARENERGY(_show, _addr)			\
	IIO_DEVICE_ATTR(lvarenergy, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_CHKSUM(_show, _addr)                       \
	IIO_DEVICE_ATTR(chksum, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_ANGLE0(_show, _addr)                       \
	IIO_DEVICE_ATTR(angle0, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_ANGLE1(_show, _addr)                       \
	IIO_DEVICE_ATTR(angle1, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_ANGLE2(_show, _addr)                       \
	IIO_DEVICE_ATTR(angle2, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_AWATTHR(_show, _addr)			\
	IIO_DEVICE_ATTR(awatthr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_BWATTHR(_show, _addr)			\
	IIO_DEVICE_ATTR(bwatthr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_CWATTHR(_show, _addr)			\
	IIO_DEVICE_ATTR(cwatthr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_AFWATTHR(_show, _addr)			\
	IIO_DEVICE_ATTR(afwatthr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_BFWATTHR(_show, _addr)			\
	IIO_DEVICE_ATTR(bfwatthr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_CFWATTHR(_show, _addr)			\
	IIO_DEVICE_ATTR(cfwatthr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_AVARHR(_show, _addr)			\
	IIO_DEVICE_ATTR(avarhr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_BVARHR(_show, _addr)			\
	IIO_DEVICE_ATTR(bvarhr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_CVARHR(_show, _addr)			\
	IIO_DEVICE_ATTR(cvarhr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_AVAHR(_show, _addr)			\
	IIO_DEVICE_ATTR(avahr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_BVAHR(_show, _addr)			\
	IIO_DEVICE_ATTR(bvahr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_CVAHR(_show, _addr)			\
	IIO_DEVICE_ATTR(cvahr, S_IRUGO, _show, NULL, _addr)

#define IIO_DEV_ATTR_IOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(ios, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_PHCAL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(phcal, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_APHCAL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(aphcal, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_BPHCAL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(bphcal, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CPHCAL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cphcal, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_APOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(apos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_AAPOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(aapos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_BAPOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(bapos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CAPOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(capos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_AVRMSGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(avrmsgain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_BVRMSGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(bvrmsgain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CVRMSGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cvrmsgain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_AIGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(aigain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_BIGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(bigain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CIGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cigain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_NIGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(nigain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_AVGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(avgain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_BVGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(bvgain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CVGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cvgain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_WGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(wgain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_WDIV(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(wdiv, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CFNUM(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cfnum, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CFDEN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cfden, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CF1DEN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cf1den, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CF2DEN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cf2den, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CF3DEN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cf3den, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_IRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(irms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vrms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_AIRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(airms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_BIRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(birms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CIRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cirms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_NIRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(nirms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_AVRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(avrms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_BVRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(bvrms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CVRMS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cvrms, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_IRMSOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(irmsos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VRMSOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vrmsos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_AIRMSOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(airmsos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_BIRMSOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(birmsos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CIRMSOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cirmsos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_AVRMSOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(avrmsos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_BVRMSOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(bvrmsos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CVRMSOS(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cvrmsos, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VAGAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vagain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_PGA_GAIN(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(pga_gain, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VADIV(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vadiv, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_LINECYC(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(linecyc, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_SAGCYC(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(sagcyc, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CFCYC(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(cfcyc, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_PEAKCYC(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(peakcyc, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_SAGLVL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(saglvl, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_IPKLVL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(ipklvl, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VPKLVL(_mode, _show, _store, _addr)                \
	IIO_DEVICE_ATTR(vpklvl, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_IPEAK(_mode, _show, _store, _addr)			\
	IIO_DEVICE_ATTR(ipeak, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_RIPEAK(_mode, _show, _store, _addr)			\
	IIO_DEVICE_ATTR(ripeak, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VPEAK(_mode, _show, _store, _addr)			\
	IIO_DEVICE_ATTR(vpeak, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_RVPEAK(_mode, _show, _store, _addr)			\
	IIO_DEVICE_ATTR(rvpeak, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_VPERIOD(_mode, _show, _store, _addr)			\
	IIO_DEVICE_ATTR(vperiod, _mode, _show, _store, _addr)

#define IIO_DEV_ATTR_CH_OFF(_num, _mode, _show, _store, _addr)			\
  IIO_DEVICE_ATTR(choff_##_num, _mode, _show, _store, _addr)

/* active energy register, AENERGY, is more than half full */
#define IIO_EVENT_ATTR_AENERGY_HALF_FULL(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(aenergy_half_full, _evlist, _show, _store, _mask)

/* a SAG on the line voltage */
#define IIO_EVENT_ATTR_LINE_VOLT_SAG(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(line_volt_sag, _evlist, _show, _store, _mask)

/*
 * Indicates the end of energy accumulation over an integer number
 * of half line cycles
 */
#define IIO_EVENT_ATTR_CYCEND(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(cycend, _evlist, _show, _store, _mask)

/* on the rising and falling edge of the the voltage waveform */
#define IIO_EVENT_ATTR_ZERO_CROSS(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(zero_cross, _evlist, _show, _store, _mask)

/* the active energy register has overflowed */
#define IIO_EVENT_ATTR_AENERGY_OVERFLOW(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(aenergy_overflow, _evlist, _show, _store, _mask)

/* the apparent energy register has overflowed */
#define IIO_EVENT_ATTR_VAENERGY_OVERFLOW(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(vaenergy_overflow, _evlist, _show, _store, _mask)

/* the active energy register, VAENERGY, is more than half full */
#define IIO_EVENT_ATTR_VAENERGY_HALF_FULL(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(vaenergy_half_full, _evlist, _show, _store, _mask)

/* the power has gone from negative to positive */
#define IIO_EVENT_ATTR_PPOS(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(ppos, _evlist, _show, _store, _mask)

/* the power has gone from positive to negative */
#define IIO_EVENT_ATTR_PNEG(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(pneg, _evlist, _show, _store, _mask)

/* waveform sample from Channel 1 has exceeded the IPKLVL value */
#define IIO_EVENT_ATTR_IPKLVL_EXC(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(ipklvl_exc, _evlist, _show, _store, _mask)

/* waveform sample from Channel 2 has exceeded the VPKLVL value */
#define IIO_EVENT_ATTR_VPKLVL_EXC(_evlist, _show, _store, _mask) \
	IIO_EVENT_ATTR_SH(vpklvl_exc, _evlist, _show, _store, _mask)

