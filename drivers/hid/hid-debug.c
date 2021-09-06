// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  (c) 1999 Andreas Gal		<gal@cs.uni-magdeburg.de>
 *  (c) 2000-2001 Vojtech Pavlik	<vojtech@ucw.cz>
 *  (c) 2007-2009 Jiri Kosina
 *
 *  HID debugging support
 */

/*
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/kfifo.h>
#include <linux/sched/signal.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>

#include <linux/hid.h>
#include <linux/hid-debug.h>

static struct dentry *hid_debug_root;

struct hid_usage_entry {
	unsigned  page;
	unsigned  usage;
	const char     *description;
};

static const struct hid_usage_entry hid_usage_table[] = {
  {  0,      0, "Undefined" },
  {  1,      0, "GenericDesktop" },
    {0, 0x01, "Pointer"},
    {0, 0x02, "Mouse"},
    {0, 0x04, "Joystick"},
    {0, 0x05, "GamePad"},
    {0, 0x06, "Keyboard"},
    {0, 0x07, "Keypad"},
    {0, 0x08, "MultiAxis"},
      {0, 0x30, "X"},
      {0, 0x31, "Y"},
      {0, 0x32, "Z"},
      {0, 0x33, "Rx"},
      {0, 0x34, "Ry"},
      {0, 0x35, "Rz"},
      {0, 0x36, "Slider"},
      {0, 0x37, "Dial"},
      {0, 0x38, "Wheel"},
      {0, 0x39, "HatSwitch"},
    {0, 0x3a, "CountedBuffer"},
      {0, 0x3b, "ByteCount"},
      {0, 0x3c, "MotionWakeup"},
      {0, 0x3d, "Start"},
      {0, 0x3e, "Select"},
      {0, 0x40, "Vx"},
      {0, 0x41, "Vy"},
      {0, 0x42, "Vz"},
      {0, 0x43, "Vbrx"},
      {0, 0x44, "Vbry"},
      {0, 0x45, "Vbrz"},
      {0, 0x46, "Vno"},
    {0, 0x80, "SystemControl"},
      {0, 0x81, "SystemPowerDown"},
      {0, 0x82, "SystemSleep"},
      {0, 0x83, "SystemWakeUp"},
      {0, 0x84, "SystemContextMenu"},
      {0, 0x85, "SystemMainMenu"},
      {0, 0x86, "SystemAppMenu"},
      {0, 0x87, "SystemMenuHelp"},
      {0, 0x88, "SystemMenuExit"},
      {0, 0x89, "SystemMenuSelect"},
      {0, 0x8a, "SystemMenuRight"},
      {0, 0x8b, "SystemMenuLeft"},
      {0, 0x8c, "SystemMenuUp"},
      {0, 0x8d, "SystemMenuDown"},
      {0, 0x90, "D-PadUp"},
      {0, 0x91, "D-PadDown"},
      {0, 0x92, "D-PadRight"},
      {0, 0x93, "D-PadLeft"},
  {  2, 0, "Simulation" },
      {0, 0xb0, "Aileron"},
      {0, 0xb1, "AileronTrim"},
      {0, 0xb2, "Anti-Torque"},
      {0, 0xb3, "Autopilot"},
      {0, 0xb4, "Chaff"},
      {0, 0xb5, "Collective"},
      {0, 0xb6, "DiveBrake"},
      {0, 0xb7, "ElectronicCountermeasures"},
      {0, 0xb8, "Elevator"},
      {0, 0xb9, "ElevatorTrim"},
      {0, 0xba, "Rudder"},
      {0, 0xbb, "Throttle"},
      {0, 0xbc, "FlightCommunications"},
      {0, 0xbd, "FlareRelease"},
      {0, 0xbe, "LandingGear"},
      {0, 0xbf, "ToeBrake"},
  {  6, 0, "GenericDeviceControls" },
      {0, 0x20, "BatteryStrength" },
      {0, 0x21, "WirelessChannel" },
      {0, 0x22, "WirelessID" },
      {0, 0x23, "DiscoverWirelessControl" },
      {0, 0x24, "SecurityCodeCharacterEntered" },
      {0, 0x25, "SecurityCodeCharactedErased" },
      {0, 0x26, "SecurityCodeCleared" },
  {  7, 0, "Keyboard" },
  {  8, 0, "LED" },
      {0, 0x01, "NumLock"},
      {0, 0x02, "CapsLock"},
      {0, 0x03, "ScrollLock"},
      {0, 0x04, "Compose"},
      {0, 0x05, "Kana"},
      {0, 0x4b, "GenericIndicator"},
  {  9, 0, "Button" },
  { 10, 0, "Ordinal" },
  { 12, 0, "Consumer" },
      {0, 0x238, "HorizontalWheel"},
  { 13, 0, "Digitizers" },
    {0, 0x01, "Digitizer"},
    {0, 0x02, "Pen"},
    {0, 0x03, "LightPen"},
    {0, 0x04, "TouchScreen"},
    {0, 0x05, "TouchPad"},
    {0, 0x0e, "DeviceConfiguration"},
    {0, 0x20, "Stylus"},
    {0, 0x21, "Puck"},
    {0, 0x22, "Finger"},
    {0, 0x23, "DeviceSettings"},
    {0, 0x30, "TipPressure"},
    {0, 0x31, "BarrelPressure"},
    {0, 0x32, "InRange"},
    {0, 0x33, "Touch"},
    {0, 0x34, "UnTouch"},
    {0, 0x35, "Tap"},
    {0, 0x39, "TabletFunctionKey"},
    {0, 0x3a, "ProgramChangeKey"},
    {0, 0x3c, "Invert"},
    {0, 0x42, "TipSwitch"},
    {0, 0x43, "SecondaryTipSwitch"},
    {0, 0x44, "BarrelSwitch"},
    {0, 0x45, "Eraser"},
    {0, 0x46, "TabletPick"},
    {0, 0x47, "Confidence"},
    {0, 0x48, "Width"},
    {0, 0x49, "Height"},
    {0, 0x51, "ContactID"},
    {0, 0x52, "InputMode"},
    {0, 0x53, "DeviceIndex"},
    {0, 0x54, "ContactCount"},
    {0, 0x55, "ContactMaximumNumber"},
    {0, 0x59, "ButtonType"},
    {0, 0x5A, "SecondaryBarrelSwitch"},
    {0, 0x5B, "TransducerSerialNumber"},
  { 15, 0, "PhysicalInterfaceDevice" },
    {0, 0x00, "Undefined"},
    {0, 0x01, "Physical_Interface_Device"},
      {0, 0x20, "Normal"},
    {0, 0x21, "Set_Effect_Report"},
      {0, 0x22, "Effect_Block_Index"},
      {0, 0x23, "Parameter_Block_Offset"},
      {0, 0x24, "ROM_Flag"},
      {0, 0x25, "Effect_Type"},
        {0, 0x26, "ET_Constant_Force"},
        {0, 0x27, "ET_Ramp"},
        {0, 0x28, "ET_Custom_Force_Data"},
        {0, 0x30, "ET_Square"},
        {0, 0x31, "ET_Sine"},
        {0, 0x32, "ET_Triangle"},
        {0, 0x33, "ET_Sawtooth_Up"},
        {0, 0x34, "ET_Sawtooth_Down"},
        {0, 0x40, "ET_Spring"},
        {0, 0x41, "ET_Damper"},
        {0, 0x42, "ET_Inertia"},
        {0, 0x43, "ET_Friction"},
      {0, 0x50, "Duration"},
      {0, 0x51, "Sample_Period"},
      {0, 0x52, "Gain"},
      {0, 0x53, "Trigger_Button"},
      {0, 0x54, "Trigger_Repeat_Interval"},
      {0, 0x55, "Axes_Enable"},
        {0, 0x56, "Direction_Enable"},
      {0, 0x57, "Direction"},
      {0, 0x58, "Type_Specific_Block_Offset"},
        {0, 0x59, "Block_Type"},
        {0, 0x5A, "Set_Envelope_Report"},
          {0, 0x5B, "Attack_Level"},
          {0, 0x5C, "Attack_Time"},
          {0, 0x5D, "Fade_Level"},
          {0, 0x5E, "Fade_Time"},
        {0, 0x5F, "Set_Condition_Report"},
        {0, 0x60, "CP_Offset"},
        {0, 0x61, "Positive_Coefficient"},
        {0, 0x62, "Negative_Coefficient"},
        {0, 0x63, "Positive_Saturation"},
        {0, 0x64, "Negative_Saturation"},
        {0, 0x65, "Dead_Band"},
      {0, 0x66, "Download_Force_Sample"},
      {0, 0x67, "Isoch_Custom_Force_Enable"},
      {0, 0x68, "Custom_Force_Data_Report"},
        {0, 0x69, "Custom_Force_Data"},
        {0, 0x6A, "Custom_Force_Vendor_Defined_Data"},
      {0, 0x6B, "Set_Custom_Force_Report"},
        {0, 0x6C, "Custom_Force_Data_Offset"},
        {0, 0x6D, "Sample_Count"},
      {0, 0x6E, "Set_Periodic_Report"},
        {0, 0x6F, "Offset"},
        {0, 0x70, "Magnitude"},
        {0, 0x71, "Phase"},
        {0, 0x72, "Period"},
      {0, 0x73, "Set_Constant_Force_Report"},
        {0, 0x74, "Set_Ramp_Force_Report"},
        {0, 0x75, "Ramp_Start"},
        {0, 0x76, "Ramp_End"},
      {0, 0x77, "Effect_Operation_Report"},
        {0, 0x78, "Effect_Operation"},
          {0, 0x79, "Op_Effect_Start"},
          {0, 0x7A, "Op_Effect_Start_Solo"},
          {0, 0x7B, "Op_Effect_Stop"},
          {0, 0x7C, "Loop_Count"},
      {0, 0x7D, "Device_Gain_Report"},
        {0, 0x7E, "Device_Gain"},
    {0, 0x7F, "PID_Pool_Report"},
      {0, 0x80, "RAM_Pool_Size"},
      {0, 0x81, "ROM_Pool_Size"},
      {0, 0x82, "ROM_Effect_Block_Count"},
      {0, 0x83, "Simultaneous_Effects_Max"},
      {0, 0x84, "Pool_Alignment"},
    {0, 0x85, "PID_Pool_Move_Report"},
      {0, 0x86, "Move_Source"},
      {0, 0x87, "Move_Destination"},
      {0, 0x88, "Move_Length"},
    {0, 0x89, "PID_Block_Load_Report"},
      {0, 0x8B, "Block_Load_Status"},
      {0, 0x8C, "Block_Load_Success"},
      {0, 0x8D, "Block_Load_Full"},
      {0, 0x8E, "Block_Load_Error"},
      {0, 0x8F, "Block_Handle"},
      {0, 0x90, "PID_Block_Free_Report"},
      {0, 0x91, "Type_Specific_Block_Handle"},
    {0, 0x92, "PID_State_Report"},
      {0, 0x94, "Effect_Playing"},
      {0, 0x95, "PID_Device_Control_Report"},
        {0, 0x96, "PID_Device_Control"},
        {0, 0x97, "DC_Enable_Actuators"},
        {0, 0x98, "DC_Disable_Actuators"},
        {0, 0x99, "DC_Stop_All_Effects"},
        {0, 0x9A, "DC_Device_Reset"},
        {0, 0x9B, "DC_Device_Pause"},
        {0, 0x9C, "DC_Device_Continue"},
      {0, 0x9F, "Device_Paused"},
      {0, 0xA0, "Actuators_Enabled"},
      {0, 0xA4, "Safety_Switch"},
      {0, 0xA5, "Actuator_Override_Switch"},
      {0, 0xA6, "Actuator_Power"},
    {0, 0xA7, "Start_Delay"},
    {0, 0xA8, "Parameter_Block_Size"},
    {0, 0xA9, "Device_Managed_Pool"},
    {0, 0xAA, "Shared_Parameter_Blocks"},
    {0, 0xAB, "Create_New_Effect_Report"},
    {0, 0xAC, "RAM_Pool_Available"},
  {  0x20, 0, "Sensor" },
    { 0x20, 0x01, "Sensor" },
    { 0x20, 0x10, "Biometric" },
      { 0x20, 0x11, "BiometricHumanPresence" },
      { 0x20, 0x12, "BiometricHumanProximity" },
      { 0x20, 0x13, "BiometricHumanTouch" },
    { 0x20, 0x20, "Electrical" },
      { 0x20, 0x21, "ElectricalCapacitance" },
      { 0x20, 0x22, "ElectricalCurrent" },
      { 0x20, 0x23, "ElectricalPower" },
      { 0x20, 0x24, "ElectricalInductance" },
      { 0x20, 0x25, "ElectricalResistance" },
      { 0x20, 0x26, "ElectricalVoltage" },
      { 0x20, 0x27, "ElectricalPoteniometer" },
      { 0x20, 0x28, "ElectricalFrequency" },
      { 0x20, 0x29, "ElectricalPeriod" },
    { 0x20, 0x30, "Environmental" },
      { 0x20, 0x31, "EnvironmentalAtmosphericPressure" },
      { 0x20, 0x32, "EnvironmentalHumidity" },
      { 0x20, 0x33, "EnvironmentalTemperature" },
      { 0x20, 0x34, "EnvironmentalWindDirection" },
      { 0x20, 0x35, "EnvironmentalWindSpeed" },
    { 0x20, 0x40, "Light" },
      { 0x20, 0x41, "LightAmbientLight" },
      { 0x20, 0x42, "LightConsumerInfrared" },
    { 0x20, 0x50, "Location" },
      { 0x20, 0x51, "LocationBroadcast" },
      { 0x20, 0x52, "LocationDeadReckoning" },
      { 0x20, 0x53, "LocationGPS" },
      { 0x20, 0x54, "LocationLookup" },
      { 0x20, 0x55, "LocationOther" },
      { 0x20, 0x56, "LocationStatic" },
      { 0x20, 0x57, "LocationTriangulation" },
    { 0x20, 0x60, "Mechanical" },
      { 0x20, 0x61, "MechanicalBooleanSwitch" },
      { 0x20, 0x62, "MechanicalBooleanSwitchArray" },
      { 0x20, 0x63, "MechanicalMultivalueSwitch" },
      { 0x20, 0x64, "MechanicalForce" },
      { 0x20, 0x65, "MechanicalPressure" },
      { 0x20, 0x66, "MechanicalStrain" },
      { 0x20, 0x67, "MechanicalWeight" },
      { 0x20, 0x68, "MechanicalHapticVibrator" },
      { 0x20, 0x69, "MechanicalHallEffectSwitch" },
    { 0x20, 0x70, "Motion" },
      { 0x20, 0x71, "MotionAccelerometer1D" },
      { 0x20, 0x72, "MotionAccelerometer2D" },
      { 0x20, 0x73, "MotionAccelerometer3D" },
      { 0x20, 0x74, "MotionGyrometer1D" },
      { 0x20, 0x75, "MotionGyrometer2D" },
      { 0x20, 0x76, "MotionGyrometer3D" },
      { 0x20, 0x77, "MotionMotionDetector" },
      { 0x20, 0x78, "MotionSpeedometer" },
      { 0x20, 0x79, "MotionAccelerometer" },
      { 0x20, 0x7A, "MotionGyrometer" },
    { 0x20, 0x80, "Orientation" },
      { 0x20, 0x81, "OrientationCompass1D" },
      { 0x20, 0x82, "OrientationCompass2D" },
      { 0x20, 0x83, "OrientationCompass3D" },
      { 0x20, 0x84, "OrientationInclinometer1D" },
      { 0x20, 0x85, "OrientationInclinometer2D" },
      { 0x20, 0x86, "OrientationInclinometer3D" },
      { 0x20, 0x87, "OrientationDistance1D" },
      { 0x20, 0x88, "OrientationDistance2D" },
      { 0x20, 0x89, "OrientationDistance3D" },
      { 0x20, 0x8A, "OrientationDeviceOrientation" },
      { 0x20, 0x8B, "OrientationCompass" },
      { 0x20, 0x8C, "OrientationInclinometer" },
      { 0x20, 0x8D, "OrientationDistance" },
    { 0x20, 0x90, "Scanner" },
      { 0x20, 0x91, "ScannerBarcode" },
      { 0x20, 0x91, "ScannerRFID" },
      { 0x20, 0x91, "ScannerNFC" },
    { 0x20, 0xA0, "Time" },
      { 0x20, 0xA1, "TimeAlarmTimer" },
      { 0x20, 0xA2, "TimeRealTimeClock" },
    { 0x20, 0xE0, "Other" },
      { 0x20, 0xE1, "OtherCustom" },
      { 0x20, 0xE2, "OtherGeneric" },
      { 0x20, 0xE3, "OtherGenericEnumerator" },
  { 0x84, 0, "Power Device" },
    { 0x84, 0x02, "PresentStatus" },
    { 0x84, 0x03, "ChangeStatus" },
    { 0x84, 0x04, "UPS" },
    { 0x84, 0x05, "PowerSupply" },
    { 0x84, 0x10, "BatterySystem" },
    { 0x84, 0x11, "BatterySystemID" },
    { 0x84, 0x12, "Battery" },
    { 0x84, 0x13, "BatteryID" },
    { 0x84, 0x14, "Charger" },
    { 0x84, 0x15, "ChargerID" },
    { 0x84, 0x16, "PowerConverter" },
    { 0x84, 0x17, "PowerConverterID" },
    { 0x84, 0x18, "OutletSystem" },
    { 0x84, 0x19, "OutletSystemID" },
    { 0x84, 0x1a, "Input" },
    { 0x84, 0x1b, "InputID" },
    { 0x84, 0x1c, "Output" },
    { 0x84, 0x1d, "OutputID" },
    { 0x84, 0x1e, "Flow" },
    { 0x84, 0x1f, "FlowID" },
    { 0x84, 0x20, "Outlet" },
    { 0x84, 0x21, "OutletID" },
    { 0x84, 0x22, "Gang" },
    { 0x84, 0x24, "PowerSummary" },
    { 0x84, 0x25, "PowerSummaryID" },
    { 0x84, 0x30, "Voltage" },
    { 0x84, 0x31, "Current" },
    { 0x84, 0x32, "Frequency" },
    { 0x84, 0x33, "ApparentPower" },
    { 0x84, 0x35, "PercentLoad" },
    { 0x84, 0x40, "ConfigVoltage" },
    { 0x84, 0x41, "ConfigCurrent" },
    { 0x84, 0x43, "ConfigApparentPower" },
    { 0x84, 0x53, "LowVoltageTransfer" },
    { 0x84, 0x54, "HighVoltageTransfer" },
    { 0x84, 0x56, "DelayBeforeStartup" },
    { 0x84, 0x57, "DelayBeforeShutdown" },
    { 0x84, 0x58, "Test" },
    { 0x84, 0x5a, "AudibleAlarmControl" },
    { 0x84, 0x60, "Present" },
    { 0x84, 0x61, "Good" },
    { 0x84, 0x62, "InternalFailure" },
    { 0x84, 0x65, "Overload" },
    { 0x84, 0x66, "OverCharged" },
    { 0x84, 0x67, "OverTemperature" },
    { 0x84, 0x68, "ShutdownRequested" },
    { 0x84, 0x69, "ShutdownImminent" },
    { 0x84, 0x6b, "SwitchOn/Off" },
    { 0x84, 0x6c, "Switchable" },
    { 0x84, 0x6d, "Used" },
    { 0x84, 0x6e, "Boost" },
    { 0x84, 0x73, "CommunicationLost" },
    { 0x84, 0xfd, "iManufacturer" },
    { 0x84, 0xfe, "iProduct" },
    { 0x84, 0xff, "iSerialNumber" },
  { 0x85, 0, "Battery System" },
    { 0x85, 0x01, "SMBBatteryMode" },
    { 0x85, 0x02, "SMBBatteryStatus" },
    { 0x85, 0x03, "SMBAlarmWarning" },
    { 0x85, 0x04, "SMBChargerMode" },
    { 0x85, 0x05, "SMBChargerStatus" },
    { 0x85, 0x06, "SMBChargerSpecInfo" },
    { 0x85, 0x07, "SMBSelectorState" },
    { 0x85, 0x08, "SMBSelectorPresets" },
    { 0x85, 0x09, "SMBSelectorInfo" },
    { 0x85, 0x29, "RemainingCapacityLimit" },
    { 0x85, 0x2c, "CapacityMode" },
    { 0x85, 0x42, "BelowRemainingCapacityLimit" },
    { 0x85, 0x44, "Charging" },
    { 0x85, 0x45, "Discharging" },
    { 0x85, 0x4b, "NeedReplacement" },
    { 0x85, 0x65, "AbsoluteStateOfCharge" },
    { 0x85, 0x66, "RemainingCapacity" },
    { 0x85, 0x68, "RunTimeToEmpty" },
    { 0x85, 0x6a, "AverageTimeToFull" },
    { 0x85, 0x83, "DesignCapacity" },
    { 0x85, 0x85, "ManufacturerDate" },
    { 0x85, 0x89, "iDeviceChemistry" },
    { 0x85, 0x8b, "Rechargeable" },
    { 0x85, 0x8f, "iOEMInformation" },
    { 0x85, 0x8d, "CapacityGranularity1" },
    { 0x85, 0xd0, "ACPresent" },
  /* pages 0xff00 to 0xffff are vendor-specific */
  { 0xffff, 0, "Vendor-specific-FF" },
  { 0, 0, NULL }
};

/* Either output directly into simple seq_file, or (if f == NULL)
 * allocate a separate buffer that will then be passed to the 'events'
 * ringbuffer.
 *
 * This is because these functions can be called both for "one-shot"
 * "rdesc" while resolving, or for blocking "events".
 *
 * This holds both for resolv_usage_page() and hid_resolv_usage().
 */
static char *resolv_usage_page(unsigned page, struct seq_file *f) {
	const struct hid_usage_entry *p;
	char *buf = NULL;

	if (!f) {
		buf = kzalloc(HID_DEBUG_BUFSIZE, GFP_ATOMIC);
		if (!buf)
			return ERR_PTR(-ENOMEM);
	}

	for (p = hid_usage_table; p->description; p++)
		if (p->page == page) {
			if (!f) {
				snprintf(buf, HID_DEBUG_BUFSIZE, "%s",
						p->description);
				return buf;
			}
			else {
				seq_printf(f, "%s", p->description);
				return NULL;
			}
		}
	if (!f)
		snprintf(buf, HID_DEBUG_BUFSIZE, "%04x", page);
	else
		seq_printf(f, "%04x", page);
	return buf;
}

char *hid_resolv_usage(unsigned usage, struct seq_file *f) {
	const struct hid_usage_entry *p;
	char *buf = NULL;
	int len = 0;

	buf = resolv_usage_page(usage >> 16, f);
	if (IS_ERR(buf)) {
		pr_err("error allocating HID debug buffer\n");
		return NULL;
	}


	if (!f) {
		len = strlen(buf);
		snprintf(buf+len, max(0, HID_DEBUG_BUFSIZE - len), ".");
		len++;
	}
	else {
		seq_printf(f, ".");
	}
	for (p = hid_usage_table; p->description; p++)
		if (p->page == (usage >> 16)) {
			for(++p; p->description && p->usage != 0; p++)
				if (p->usage == (usage & 0xffff)) {
					if (!f)
						snprintf(buf + len,
							max(0,HID_DEBUG_BUFSIZE - len - 1),
							"%s", p->description);
					else
						seq_printf(f,
							"%s",
							p->description);
					return buf;
				}
			break;
		}
	if (!f)
		snprintf(buf + len, max(0, HID_DEBUG_BUFSIZE - len - 1),
				"%04x", usage & 0xffff);
	else
		seq_printf(f, "%04x", usage & 0xffff);
	return buf;
}
EXPORT_SYMBOL_GPL(hid_resolv_usage);

static void tab(int n, struct seq_file *f) {
	seq_printf(f, "%*s", n, "");
}

void hid_dump_field(struct hid_field *field, int n, struct seq_file *f) {
	int j;

	if (field->physical) {
		tab(n, f);
		seq_printf(f, "Physical(");
		hid_resolv_usage(field->physical, f); seq_printf(f, ")\n");
	}
	if (field->logical) {
		tab(n, f);
		seq_printf(f, "Logical(");
		hid_resolv_usage(field->logical, f); seq_printf(f, ")\n");
	}
	if (field->application) {
		tab(n, f);
		seq_printf(f, "Application(");
		hid_resolv_usage(field->application, f); seq_printf(f, ")\n");
	}
	tab(n, f); seq_printf(f, "Usage(%d)\n", field->maxusage);
	for (j = 0; j < field->maxusage; j++) {
		tab(n+2, f); hid_resolv_usage(field->usage[j].hid, f); seq_printf(f, "\n");
	}
	if (field->logical_minimum != field->logical_maximum) {
		tab(n, f); seq_printf(f, "Logical Minimum(%d)\n", field->logical_minimum);
		tab(n, f); seq_printf(f, "Logical Maximum(%d)\n", field->logical_maximum);
	}
	if (field->physical_minimum != field->physical_maximum) {
		tab(n, f); seq_printf(f, "Physical Minimum(%d)\n", field->physical_minimum);
		tab(n, f); seq_printf(f, "Physical Maximum(%d)\n", field->physical_maximum);
	}
	if (field->unit_exponent) {
		tab(n, f); seq_printf(f, "Unit Exponent(%d)\n", field->unit_exponent);
	}
	if (field->unit) {
		static const char *systems[5] = { "None", "SI Linear", "SI Rotation", "English Linear", "English Rotation" };
		static const char *units[5][8] = {
			{ "None", "None", "None", "None", "None", "None", "None", "None" },
			{ "None", "Centimeter", "Gram", "Seconds", "Kelvin",     "Ampere", "Candela", "None" },
			{ "None", "Radians",    "Gram", "Seconds", "Kelvin",     "Ampere", "Candela", "None" },
			{ "None", "Inch",       "Slug", "Seconds", "Fahrenheit", "Ampere", "Candela", "None" },
			{ "None", "Degrees",    "Slug", "Seconds", "Fahrenheit", "Ampere", "Candela", "None" }
		};

		int i;
		int sys;
                __u32 data = field->unit;

		/* First nibble tells us which system we're in. */
		sys = data & 0xf;
		data >>= 4;

		if(sys > 4) {
			tab(n, f); seq_printf(f, "Unit(Invalid)\n");
		}
		else {
			int earlier_unit = 0;

			tab(n, f); seq_printf(f, "Unit(%s : ", systems[sys]);

			for (i=1 ; i<sizeof(__u32)*2 ; i++) {
				char nibble = data & 0xf;
				data >>= 4;
				if (nibble != 0) {
					if(earlier_unit++ > 0)
						seq_printf(f, "*");
					seq_printf(f, "%s", units[sys][i]);
					if(nibble != 1) {
						/* This is a _signed_ nibble(!) */

						int val = nibble & 0x7;
						if(nibble & 0x08)
							val = -((0x7 & ~val) +1);
						seq_printf(f, "^%d", val);
					}
				}
			}
			seq_printf(f, ")\n");
		}
	}
	tab(n, f); seq_printf(f, "Report Size(%u)\n", field->report_size);
	tab(n, f); seq_printf(f, "Report Count(%u)\n", field->report_count);
	tab(n, f); seq_printf(f, "Report Offset(%u)\n", field->report_offset);

	tab(n, f); seq_printf(f, "Flags( ");
	j = field->flags;
	seq_printf(f, "%s", HID_MAIN_ITEM_CONSTANT & j ? "Constant " : "");
	seq_printf(f, "%s", HID_MAIN_ITEM_VARIABLE & j ? "Variable " : "Array ");
	seq_printf(f, "%s", HID_MAIN_ITEM_RELATIVE & j ? "Relative " : "Absolute ");
	seq_printf(f, "%s", HID_MAIN_ITEM_WRAP & j ? "Wrap " : "");
	seq_printf(f, "%s", HID_MAIN_ITEM_NONLINEAR & j ? "NonLinear " : "");
	seq_printf(f, "%s", HID_MAIN_ITEM_NO_PREFERRED & j ? "NoPreferredState " : "");
	seq_printf(f, "%s", HID_MAIN_ITEM_NULL_STATE & j ? "NullState " : "");
	seq_printf(f, "%s", HID_MAIN_ITEM_VOLATILE & j ? "Volatile " : "");
	seq_printf(f, "%s", HID_MAIN_ITEM_BUFFERED_BYTE & j ? "BufferedByte " : "");
	seq_printf(f, ")\n");
}
EXPORT_SYMBOL_GPL(hid_dump_field);

void hid_dump_device(struct hid_device *device, struct seq_file *f)
{
	struct hid_report_enum *report_enum;
	struct hid_report *report;
	struct list_head *list;
	unsigned i,k;
	static const char *table[] = {"INPUT", "OUTPUT", "FEATURE"};

	for (i = 0; i < HID_REPORT_TYPES; i++) {
		report_enum = device->report_enum + i;
		list = report_enum->report_list.next;
		while (list != &report_enum->report_list) {
			report = (struct hid_report *) list;
			tab(2, f);
			seq_printf(f, "%s", table[i]);
			if (report->id)
				seq_printf(f, "(%d)", report->id);
			seq_printf(f, "[%s]", table[report->type]);
			seq_printf(f, "\n");
			for (k = 0; k < report->maxfield; k++) {
				tab(4, f);
				seq_printf(f, "Field(%d)\n", k);
				hid_dump_field(report->field[k], 6, f);
			}
			list = list->next;
		}
	}
}
EXPORT_SYMBOL_GPL(hid_dump_device);

/* enqueue string to 'events' ring buffer */
void hid_debug_event(struct hid_device *hdev, char *buf)
{
	struct hid_debug_list *list;
	unsigned long flags;

	spin_lock_irqsave(&hdev->debug_list_lock, flags);
	list_for_each_entry(list, &hdev->debug_list, node)
		kfifo_in(&list->hid_debug_fifo, buf, strlen(buf));
	spin_unlock_irqrestore(&hdev->debug_list_lock, flags);

	wake_up_interruptible(&hdev->debug_wait);
}
EXPORT_SYMBOL_GPL(hid_debug_event);

void hid_dump_report(struct hid_device *hid, int type, u8 *data,
		int size)
{
	struct hid_report_enum *report_enum;
	char *buf;
	unsigned int i;

	buf = kmalloc(HID_DEBUG_BUFSIZE, GFP_ATOMIC);

	if (!buf)
		return;

	report_enum = hid->report_enum + type;

	/* dump the report */
	snprintf(buf, HID_DEBUG_BUFSIZE - 1,
			"\nreport (size %u) (%snumbered) = ", size,
			report_enum->numbered ? "" : "un");
	hid_debug_event(hid, buf);

	for (i = 0; i < size; i++) {
		snprintf(buf, HID_DEBUG_BUFSIZE - 1,
				" %02x", data[i]);
		hid_debug_event(hid, buf);
	}
	hid_debug_event(hid, "\n");
	kfree(buf);
}
EXPORT_SYMBOL_GPL(hid_dump_report);

void hid_dump_input(struct hid_device *hdev, struct hid_usage *usage, __s32 value)
{
	char *buf;
	int len;

	buf = hid_resolv_usage(usage->hid, NULL);
	if (!buf)
		return;
	len = strlen(buf);
	snprintf(buf + len, HID_DEBUG_BUFSIZE - len - 1, " = %d\n", value);

	hid_debug_event(hdev, buf);

	kfree(buf);
	wake_up_interruptible(&hdev->debug_wait);
}
EXPORT_SYMBOL_GPL(hid_dump_input);

static const char *events[EV_MAX + 1] = {
	[EV_SYN] = "Sync",			[EV_KEY] = "Key",
	[EV_REL] = "Relative",			[EV_ABS] = "Absolute",
	[EV_MSC] = "Misc",			[EV_LED] = "LED",
	[EV_SND] = "Sound",			[EV_REP] = "Repeat",
	[EV_FF] = "ForceFeedback",		[EV_PWR] = "Power",
	[EV_FF_STATUS] = "ForceFeedbackStatus",
};

static const char *syncs[3] = {
	[SYN_REPORT] = "Report",		[SYN_CONFIG] = "Config",
	[SYN_MT_REPORT] = "MT Report",
};

static const char *keys[KEY_MAX + 1] = {
	[KEY_RESERVED] = "Reserved",		[KEY_ESC] = "Esc",
	[KEY_1] = "1",				[KEY_2] = "2",
	[KEY_3] = "3",				[KEY_4] = "4",
	[KEY_5] = "5",				[KEY_6] = "6",
	[KEY_7] = "7",				[KEY_8] = "8",
	[KEY_9] = "9",				[KEY_0] = "0",
	[KEY_MINUS] = "Minus",			[KEY_EQUAL] = "Equal",
	[KEY_BACKSPACE] = "Backspace",		[KEY_TAB] = "Tab",
	[KEY_Q] = "Q",				[KEY_W] = "W",
	[KEY_E] = "E",				[KEY_R] = "R",
	[KEY_T] = "T",				[KEY_Y] = "Y",
	[KEY_U] = "U",				[KEY_I] = "I",
	[KEY_O] = "O",				[KEY_P] = "P",
	[KEY_LEFTBRACE] = "LeftBrace",		[KEY_RIGHTBRACE] = "RightBrace",
	[KEY_ENTER] = "Enter",			[KEY_LEFTCTRL] = "LeftControl",
	[KEY_A] = "A",				[KEY_S] = "S",
	[KEY_D] = "D",				[KEY_F] = "F",
	[KEY_G] = "G",				[KEY_H] = "H",
	[KEY_J] = "J",				[KEY_K] = "K",
	[KEY_L] = "L",				[KEY_SEMICOLON] = "Semicolon",
	[KEY_APOSTROPHE] = "Apostrophe",	[KEY_GRAVE] = "Grave",
	[KEY_LEFTSHIFT] = "LeftShift",		[KEY_BACKSLASH] = "BackSlash",
	[KEY_Z] = "Z",				[KEY_X] = "X",
	[KEY_C] = "C",				[KEY_V] = "V",
	[KEY_B] = "B",				[KEY_N] = "N",
	[KEY_M] = "M",				[KEY_COMMA] = "Comma",
	[KEY_DOT] = "Dot",			[KEY_SLASH] = "Slash",
	[KEY_RIGHTSHIFT] = "RightShift",	[KEY_KPASTERISK] = "KPAsterisk",
	[KEY_LEFTALT] = "LeftAlt",		[KEY_SPACE] = "Space",
	[KEY_CAPSLOCK] = "CapsLock",		[KEY_F1] = "F1",
	[KEY_F2] = "F2",			[KEY_F3] = "F3",
	[KEY_F4] = "F4",			[KEY_F5] = "F5",
	[KEY_F6] = "F6",			[KEY_F7] = "F7",
	[KEY_F8] = "F8",			[KEY_F9] = "F9",
	[KEY_F10] = "F10",			[KEY_NUMLOCK] = "NumLock",
	[KEY_SCROLLLOCK] = "ScrollLock",	[KEY_KP7] = "KP7",
	[KEY_KP8] = "KP8",			[KEY_KP9] = "KP9",
	[KEY_KPMINUS] = "KPMinus",		[KEY_KP4] = "KP4",
	[KEY_KP5] = "KP5",			[KEY_KP6] = "KP6",
	[KEY_KPPLUS] = "KPPlus",		[KEY_KP1] = "KP1",
	[KEY_KP2] = "KP2",			[KEY_KP3] = "KP3",
	[KEY_KP0] = "KP0",			[KEY_KPDOT] = "KPDot",
	[KEY_ZENKAKUHANKAKU] = "Zenkaku/Hankaku", [KEY_102ND] = "102nd",
	[KEY_F11] = "F11",			[KEY_F12] = "F12",
	[KEY_RO] = "RO",			[KEY_KATAKANA] = "Katakana",
	[KEY_HIRAGANA] = "HIRAGANA",		[KEY_HENKAN] = "Henkan",
	[KEY_KATAKANAHIRAGANA] = "Katakana/Hiragana", [KEY_MUHENKAN] = "Muhenkan",
	[KEY_KPJPCOMMA] = "KPJpComma",		[KEY_KPENTER] = "KPEnter",
	[KEY_RIGHTCTRL] = "RightCtrl",		[KEY_KPSLASH] = "KPSlash",
	[KEY_SYSRQ] = "SysRq",			[KEY_RIGHTALT] = "RightAlt",
	[KEY_LINEFEED] = "LineFeed",		[KEY_HOME] = "Home",
	[KEY_UP] = "Up",			[KEY_PAGEUP] = "PageUp",
	[KEY_LEFT] = "Left",			[KEY_RIGHT] = "Right",
	[KEY_END] = "End",			[KEY_DOWN] = "Down",
	[KEY_PAGEDOWN] = "PageDown",		[KEY_INSERT] = "Insert",
	[KEY_DELETE] = "Delete",		[KEY_MACRO] = "Macro",
	[KEY_MUTE] = "Mute",			[KEY_VOLUMEDOWN] = "VolumeDown",
	[KEY_VOLUMEUP] = "VolumeUp",		[KEY_POWER] = "Power",
	[KEY_KPEQUAL] = "KPEqual",		[KEY_KPPLUSMINUS] = "KPPlusMinus",
	[KEY_PAUSE] = "Pause",			[KEY_KPCOMMA] = "KPComma",
	[KEY_HANGUEL] = "Hangeul",		[KEY_HANJA] = "Hanja",
	[KEY_YEN] = "Yen",			[KEY_LEFTMETA] = "LeftMeta",
	[KEY_RIGHTMETA] = "RightMeta",		[KEY_COMPOSE] = "Compose",
	[KEY_STOP] = "Stop",			[KEY_AGAIN] = "Again",
	[KEY_PROPS] = "Props",			[KEY_UNDO] = "Undo",
	[KEY_FRONT] = "Front",			[KEY_COPY] = "Copy",
	[KEY_OPEN] = "Open",			[KEY_PASTE] = "Paste",
	[KEY_FIND] = "Find",			[KEY_CUT] = "Cut",
	[KEY_HELP] = "Help",			[KEY_MENU] = "Menu",
	[KEY_CALC] = "Calc",			[KEY_SETUP] = "Setup",
	[KEY_SLEEP] = "Sleep",			[KEY_WAKEUP] = "WakeUp",
	[KEY_FILE] = "File",			[KEY_SENDFILE] = "SendFile",
	[KEY_DELETEFILE] = "DeleteFile",	[KEY_XFER] = "X-fer",
	[KEY_PROG1] = "Prog1",			[KEY_PROG2] = "Prog2",
	[KEY_WWW] = "WWW",			[KEY_MSDOS] = "MSDOS",
	[KEY_COFFEE] = "Coffee",		[KEY_ROTATE_DISPLAY] = "RotateDisplay",
	[KEY_CYCLEWINDOWS] = "CycleWindows",	[KEY_MAIL] = "Mail",
	[KEY_BOOKMARKS] = "Bookmarks",		[KEY_COMPUTER] = "Computer",
	[KEY_BACK] = "Back",			[KEY_FORWARD] = "Forward",
	[KEY_CLOSECD] = "CloseCD",		[KEY_EJECTCD] = "EjectCD",
	[KEY_EJECTCLOSECD] = "EjectCloseCD",	[KEY_NEXTSONG] = "NextSong",
	[KEY_PLAYPAUSE] = "PlayPause",		[KEY_PREVIOUSSONG] = "PreviousSong",
	[KEY_STOPCD] = "StopCD",		[KEY_RECORD] = "Record",
	[KEY_REWIND] = "Rewind",		[KEY_PHONE] = "Phone",
	[KEY_ISO] = "ISOKey",			[KEY_CONFIG] = "Config",
	[KEY_HOMEPAGE] = "HomePage",		[KEY_REFRESH] = "Refresh",
	[KEY_EXIT] = "Exit",			[KEY_MOVE] = "Move",
	[KEY_EDIT] = "Edit",			[KEY_SCROLLUP] = "ScrollUp",
	[KEY_SCROLLDOWN] = "ScrollDown",	[KEY_KPLEFTPAREN] = "KPLeftParenthesis",
	[KEY_KPRIGHTPAREN] = "KPRightParenthesis", [KEY_NEW] = "New",
	[KEY_REDO] = "Redo",			[KEY_F13] = "F13",
	[KEY_F14] = "F14",			[KEY_F15] = "F15",
	[KEY_F16] = "F16",			[KEY_F17] = "F17",
	[KEY_F18] = "F18",			[KEY_F19] = "F19",
	[KEY_F20] = "F20",			[KEY_F21] = "F21",
	[KEY_F22] = "F22",			[KEY_F23] = "F23",
	[KEY_F24] = "F24",			[KEY_PLAYCD] = "PlayCD",
	[KEY_PAUSECD] = "PauseCD",		[KEY_PROG3] = "Prog3",
	[KEY_PROG4] = "Prog4",			[KEY_SUSPEND] = "Suspend",
	[KEY_CLOSE] = "Close",			[KEY_PLAY] = "Play",
	[KEY_FASTFORWARD] = "FastForward",	[KEY_BASSBOOST] = "BassBoost",
	[KEY_PRINT] = "Print",			[KEY_HP] = "HP",
	[KEY_CAMERA] = "Camera",		[KEY_SOUND] = "Sound",
	[KEY_QUESTION] = "Question",		[KEY_EMAIL] = "Email",
	[KEY_CHAT] = "Chat",			[KEY_SEARCH] = "Search",
	[KEY_CONNECT] = "Connect",		[KEY_FINANCE] = "Finance",
	[KEY_SPORT] = "Sport",			[KEY_SHOP] = "Shop",
	[KEY_ALTERASE] = "AlternateErase",	[KEY_CANCEL] = "Cancel",
	[KEY_BRIGHTNESSDOWN] = "BrightnessDown", [KEY_BRIGHTNESSUP] = "BrightnessUp",
	[KEY_MEDIA] = "Media",			[KEY_UNKNOWN] = "Unknown",
	[BTN_DPAD_UP] = "BtnDPadUp",		[BTN_DPAD_DOWN] = "BtnDPadDown",
	[BTN_DPAD_LEFT] = "BtnDPadLeft",	[BTN_DPAD_RIGHT] = "BtnDPadRight",
	[BTN_0] = "Btn0",			[BTN_1] = "Btn1",
	[BTN_2] = "Btn2",			[BTN_3] = "Btn3",
	[BTN_4] = "Btn4",			[BTN_5] = "Btn5",
	[BTN_6] = "Btn6",			[BTN_7] = "Btn7",
	[BTN_8] = "Btn8",			[BTN_9] = "Btn9",
	[BTN_LEFT] = "LeftBtn",			[BTN_RIGHT] = "RightBtn",
	[BTN_MIDDLE] = "MiddleBtn",		[BTN_SIDE] = "SideBtn",
	[BTN_EXTRA] = "ExtraBtn",		[BTN_FORWARD] = "ForwardBtn",
	[BTN_BACK] = "BackBtn",			[BTN_TASK] = "TaskBtn",
	[BTN_TRIGGER] = "Trigger",		[BTN_THUMB] = "ThumbBtn",
	[BTN_THUMB2] = "ThumbBtn2",		[BTN_TOP] = "TopBtn",
	[BTN_TOP2] = "TopBtn2",			[BTN_PINKIE] = "PinkieBtn",
	[BTN_BASE] = "BaseBtn",			[BTN_BASE2] = "BaseBtn2",
	[BTN_BASE3] = "BaseBtn3",		[BTN_BASE4] = "BaseBtn4",
	[BTN_BASE5] = "BaseBtn5",		[BTN_BASE6] = "BaseBtn6",
	[BTN_DEAD] = "BtnDead",			[BTN_A] = "BtnA",
	[BTN_B] = "BtnB",			[BTN_C] = "BtnC",
	[BTN_X] = "BtnX",			[BTN_Y] = "BtnY",
	[BTN_Z] = "BtnZ",			[BTN_TL] = "BtnTL",
	[BTN_TR] = "BtnTR",			[BTN_TL2] = "BtnTL2",
	[BTN_TR2] = "BtnTR2",			[BTN_SELECT] = "BtnSelect",
	[BTN_START] = "BtnStart",		[BTN_MODE] = "BtnMode",
	[BTN_THUMBL] = "BtnThumbL",		[BTN_THUMBR] = "BtnThumbR",
	[BTN_TOOL_PEN] = "ToolPen",		[BTN_TOOL_RUBBER] = "ToolRubber",
	[BTN_TOOL_BRUSH] = "ToolBrush",		[BTN_TOOL_PENCIL] = "ToolPencil",
	[BTN_TOOL_AIRBRUSH] = "ToolAirbrush",	[BTN_TOOL_FINGER] = "ToolFinger",
	[BTN_TOOL_MOUSE] = "ToolMouse",		[BTN_TOOL_LENS] = "ToolLens",
	[BTN_TOUCH] = "Touch",			[BTN_STYLUS] = "Stylus",
	[BTN_STYLUS2] = "Stylus2",		[BTN_TOOL_DOUBLETAP] = "ToolDoubleTap",
	[BTN_TOOL_TRIPLETAP] = "ToolTripleTap",	[BTN_TOOL_QUADTAP] = "ToolQuadrupleTap",
	[BTN_GEAR_DOWN] = "WheelBtn",
	[BTN_GEAR_UP] = "Gear up",		[KEY_OK] = "Ok",
	[KEY_SELECT] = "Select",		[KEY_GOTO] = "Goto",
	[KEY_CLEAR] = "Clear",			[KEY_POWER2] = "Power2",
	[KEY_OPTION] = "Option",		[KEY_INFO] = "Info",
	[KEY_TIME] = "Time",			[KEY_VENDOR] = "Vendor",
	[KEY_ARCHIVE] = "Archive",		[KEY_PROGRAM] = "Program",
	[KEY_CHANNEL] = "Channel",		[KEY_FAVORITES] = "Favorites",
	[KEY_EPG] = "EPG",			[KEY_PVR] = "PVR",
	[KEY_MHP] = "MHP",			[KEY_LANGUAGE] = "Language",
	[KEY_TITLE] = "Title",			[KEY_SUBTITLE] = "Subtitle",
	[KEY_ANGLE] = "Angle",			[KEY_ZOOM] = "Zoom",
	[KEY_MODE] = "Mode",			[KEY_KEYBOARD] = "Keyboard",
	[KEY_SCREEN] = "Screen",		[KEY_PC] = "PC",
	[KEY_TV] = "TV",			[KEY_TV2] = "TV2",
	[KEY_VCR] = "VCR",			[KEY_VCR2] = "VCR2",
	[KEY_SAT] = "Sat",			[KEY_SAT2] = "Sat2",
	[KEY_CD] = "CD",			[KEY_TAPE] = "Tape",
	[KEY_RADIO] = "Radio",			[KEY_TUNER] = "Tuner",
	[KEY_PLAYER] = "Player",		[KEY_TEXT] = "Text",
	[KEY_DVD] = "DVD",			[KEY_AUX] = "Aux",
	[KEY_MP3] = "MP3",			[KEY_AUDIO] = "Audio",
	[KEY_VIDEO] = "Video",			[KEY_DIRECTORY] = "Directory",
	[KEY_LIST] = "List",			[KEY_MEMO] = "Memo",
	[KEY_CALENDAR] = "Calendar",		[KEY_RED] = "Red",
	[KEY_GREEN] = "Green",			[KEY_YELLOW] = "Yellow",
	[KEY_BLUE] = "Blue",			[KEY_CHANNELUP] = "ChannelUp",
	[KEY_CHANNELDOWN] = "ChannelDown",	[KEY_FIRST] = "First",
	[KEY_LAST] = "Last",			[KEY_AB] = "AB",
	[KEY_NEXT] = "Next",			[KEY_RESTART] = "Restart",
	[KEY_SLOW] = "Slow",			[KEY_SHUFFLE] = "Shuffle",
	[KEY_BREAK] = "Break",			[KEY_PREVIOUS] = "Previous",
	[KEY_DIGITS] = "Digits",		[KEY_TEEN] = "TEEN",
	[KEY_TWEN] = "TWEN",			[KEY_DEL_EOL] = "DeleteEOL",
	[KEY_DEL_EOS] = "DeleteEOS",		[KEY_INS_LINE] = "InsertLine",
	[KEY_DEL_LINE] = "DeleteLine",
	[KEY_SEND] = "Send",			[KEY_REPLY] = "Reply",
	[KEY_FORWARDMAIL] = "ForwardMail",	[KEY_SAVE] = "Save",
	[KEY_DOCUMENTS] = "Documents",		[KEY_SPELLCHECK] = "SpellCheck",
	[KEY_LOGOFF] = "Logoff",
	[KEY_FN] = "Fn",			[KEY_FN_ESC] = "Fn+ESC",
	[KEY_FN_1] = "Fn+1",			[KEY_FN_2] = "Fn+2",
	[KEY_FN_B] = "Fn+B",			[KEY_FN_D] = "Fn+D",
	[KEY_FN_E] = "Fn+E",			[KEY_FN_F] = "Fn+F",
	[KEY_FN_S] = "Fn+S",
	[KEY_FN_F1] = "Fn+F1",			[KEY_FN_F2] = "Fn+F2",
	[KEY_FN_F3] = "Fn+F3",			[KEY_FN_F4] = "Fn+F4",
	[KEY_FN_F5] = "Fn+F5",			[KEY_FN_F6] = "Fn+F6",
	[KEY_FN_F7] = "Fn+F7",			[KEY_FN_F8] = "Fn+F8",
	[KEY_FN_F9] = "Fn+F9",			[KEY_FN_F10] = "Fn+F10",
	[KEY_FN_F11] = "Fn+F11",		[KEY_FN_F12] = "Fn+F12",
	[KEY_KBDILLUMTOGGLE] = "KbdIlluminationToggle",
	[KEY_KBDILLUMDOWN] = "KbdIlluminationDown",
	[KEY_KBDILLUMUP] = "KbdIlluminationUp",
	[KEY_SWITCHVIDEOMODE] = "SwitchVideoMode",
	[KEY_BUTTONCONFIG] = "ButtonConfig",
	[KEY_TASKMANAGER] = "TaskManager",
	[KEY_JOURNAL] = "Journal",
	[KEY_CONTROLPANEL] = "ControlPanel",
	[KEY_APPSELECT] = "AppSelect",
	[KEY_SCREENSAVER] = "ScreenSaver",
	[KEY_VOICECOMMAND] = "VoiceCommand",
	[KEY_ASSISTANT] = "Assistant",
	[KEY_KBD_LAYOUT_NEXT] = "KbdLayoutNext",
	[KEY_EMOJI_PICKER] = "EmojiPicker",
	[KEY_BRIGHTNESS_MIN] = "BrightnessMin",
	[KEY_BRIGHTNESS_MAX] = "BrightnessMax",
	[KEY_BRIGHTNESS_AUTO] = "BrightnessAuto",
	[KEY_KBDINPUTASSIST_PREV] = "KbdInputAssistPrev",
	[KEY_KBDINPUTASSIST_NEXT] = "KbdInputAssistNext",
	[KEY_KBDINPUTASSIST_PREVGROUP] = "KbdInputAssistPrevGroup",
	[KEY_KBDINPUTASSIST_NEXTGROUP] = "KbdInputAssistNextGroup",
	[KEY_KBDINPUTASSIST_ACCEPT] = "KbdInputAssistAccept",
	[KEY_KBDINPUTASSIST_CANCEL] = "KbdInputAssistCancel",
};

static const char *relatives[REL_MAX + 1] = {
	[REL_X] = "X",			[REL_Y] = "Y",
	[REL_Z] = "Z",			[REL_RX] = "Rx",
	[REL_RY] = "Ry",		[REL_RZ] = "Rz",
	[REL_HWHEEL] = "HWheel",	[REL_DIAL] = "Dial",
	[REL_WHEEL] = "Wheel",		[REL_MISC] = "Misc",
};

static const char *absolutes[ABS_CNT] = {
	[ABS_X] = "X",			[ABS_Y] = "Y",
	[ABS_Z] = "Z",			[ABS_RX] = "Rx",
	[ABS_RY] = "Ry",		[ABS_RZ] = "Rz",
	[ABS_THROTTLE] = "Throttle",	[ABS_RUDDER] = "Rudder",
	[ABS_WHEEL] = "Wheel",		[ABS_GAS] = "Gas",
	[ABS_BRAKE] = "Brake",		[ABS_HAT0X] = "Hat0X",
	[ABS_HAT0Y] = "Hat0Y",		[ABS_HAT1X] = "Hat1X",
	[ABS_HAT1Y] = "Hat1Y",		[ABS_HAT2X] = "Hat2X",
	[ABS_HAT2Y] = "Hat2Y",		[ABS_HAT3X] = "Hat3X",
	[ABS_HAT3Y] = "Hat 3Y",		[ABS_PRESSURE] = "Pressure",
	[ABS_DISTANCE] = "Distance",	[ABS_TILT_X] = "XTilt",
	[ABS_TILT_Y] = "YTilt",		[ABS_TOOL_WIDTH] = "ToolWidth",
	[ABS_VOLUME] = "Volume",	[ABS_MISC] = "Misc",
	[ABS_MT_TOUCH_MAJOR] = "MTMajor",
	[ABS_MT_TOUCH_MINOR] = "MTMinor",
	[ABS_MT_WIDTH_MAJOR] = "MTMajorW",
	[ABS_MT_WIDTH_MINOR] = "MTMinorW",
	[ABS_MT_ORIENTATION] = "MTOrientation",
	[ABS_MT_POSITION_X] = "MTPositionX",
	[ABS_MT_POSITION_Y] = "MTPositionY",
	[ABS_MT_TOOL_TYPE] = "MTToolType",
	[ABS_MT_BLOB_ID] = "MTBlobID",
};

static const char *misc[MSC_MAX + 1] = {
	[MSC_SERIAL] = "Serial",	[MSC_PULSELED] = "Pulseled",
	[MSC_GESTURE] = "Gesture",	[MSC_RAW] = "RawData"
};

static const char *leds[LED_MAX + 1] = {
	[LED_NUML] = "NumLock",		[LED_CAPSL] = "CapsLock",
	[LED_SCROLLL] = "ScrollLock",	[LED_COMPOSE] = "Compose",
	[LED_KANA] = "Kana",		[LED_SLEEP] = "Sleep",
	[LED_SUSPEND] = "Suspend",	[LED_MUTE] = "Mute",
	[LED_MISC] = "Misc",
};

static const char *repeats[REP_MAX + 1] = {
	[REP_DELAY] = "Delay",		[REP_PERIOD] = "Period"
};

static const char *sounds[SND_MAX + 1] = {
	[SND_CLICK] = "Click",		[SND_BELL] = "Bell",
	[SND_TONE] = "Tone"
};

static const char **names[EV_MAX + 1] = {
	[EV_SYN] = syncs,			[EV_KEY] = keys,
	[EV_REL] = relatives,			[EV_ABS] = absolutes,
	[EV_MSC] = misc,			[EV_LED] = leds,
	[EV_SND] = sounds,			[EV_REP] = repeats,
};

static void hid_resolv_event(__u8 type, __u16 code, struct seq_file *f)
{
	seq_printf(f, "%s.%s", events[type] ? events[type] : "?",
		names[type] ? (names[type][code] ? names[type][code] : "?") : "?");
}

static void hid_dump_input_mapping(struct hid_device *hid, struct seq_file *f)
{
	int i, j, k;
	struct hid_report *report;
	struct hid_usage *usage;

	for (k = HID_INPUT_REPORT; k <= HID_OUTPUT_REPORT; k++) {
		list_for_each_entry(report, &hid->report_enum[k].report_list, list) {
			for (i = 0; i < report->maxfield; i++) {
				for ( j = 0; j < report->field[i]->maxusage; j++) {
					usage = report->field[i]->usage + j;
					hid_resolv_usage(usage->hid, f);
					seq_printf(f, " ---> ");
					hid_resolv_event(usage->type, usage->code, f);
					seq_printf(f, "\n");
				}
			}
		}
	}

}

static int hid_debug_rdesc_show(struct seq_file *f, void *p)
{
	struct hid_device *hdev = f->private;
	const __u8 *rdesc = hdev->rdesc;
	unsigned rsize = hdev->rsize;
	int i;

	if (!rdesc) {
		rdesc = hdev->dev_rdesc;
		rsize = hdev->dev_rsize;
	}

	/* dump HID report descriptor */
	for (i = 0; i < rsize; i++)
		seq_printf(f, "%02x ", rdesc[i]);
	seq_printf(f, "\n\n");

	/* dump parsed data and input mappings */
	if (down_interruptible(&hdev->driver_input_lock))
		return 0;

	hid_dump_device(hdev, f);
	seq_printf(f, "\n");
	hid_dump_input_mapping(hdev, f);

	up(&hdev->driver_input_lock);

	return 0;
}

static int hid_debug_events_open(struct inode *inode, struct file *file)
{
	int err = 0;
	struct hid_debug_list *list;
	unsigned long flags;

	if (!(list = kzalloc(sizeof(struct hid_debug_list), GFP_KERNEL))) {
		err = -ENOMEM;
		goto out;
	}

	err = kfifo_alloc(&list->hid_debug_fifo, HID_DEBUG_FIFOSIZE, GFP_KERNEL);
	if (err) {
		kfree(list);
		goto out;
	}
	list->hdev = (struct hid_device *) inode->i_private;
	file->private_data = list;
	mutex_init(&list->read_mutex);

	spin_lock_irqsave(&list->hdev->debug_list_lock, flags);
	list_add_tail(&list->node, &list->hdev->debug_list);
	spin_unlock_irqrestore(&list->hdev->debug_list_lock, flags);

out:
	return err;
}

static ssize_t hid_debug_events_read(struct file *file, char __user *buffer,
		size_t count, loff_t *ppos)
{
	struct hid_debug_list *list = file->private_data;
	int ret = 0, copied;
	DECLARE_WAITQUEUE(wait, current);

	mutex_lock(&list->read_mutex);
	if (kfifo_is_empty(&list->hid_debug_fifo)) {
		add_wait_queue(&list->hdev->debug_wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);

		while (kfifo_is_empty(&list->hid_debug_fifo)) {
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}

			/* if list->hdev is NULL we cannot remove_wait_queue().
			 * if list->hdev->debug is 0 then hid_debug_unregister()
			 * was already called and list->hdev is being destroyed.
			 * if we add remove_wait_queue() here we can hit a race.
			 */
			if (!list->hdev || !list->hdev->debug) {
				ret = -EIO;
				set_current_state(TASK_RUNNING);
				goto out;
			}

			if (file->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}

			/* allow O_NONBLOCK from other threads */
			mutex_unlock(&list->read_mutex);
			schedule();
			mutex_lock(&list->read_mutex);
			set_current_state(TASK_INTERRUPTIBLE);
		}

		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&list->hdev->debug_wait, &wait);

		if (ret)
			goto out;
	}

	/* pass the fifo content to userspace, locking is not needed with only
	 * one concurrent reader and one concurrent writer
	 */
	ret = kfifo_to_user(&list->hid_debug_fifo, buffer, count, &copied);
	if (ret)
		goto out;
	ret = copied;
out:
	mutex_unlock(&list->read_mutex);
	return ret;
}

static __poll_t hid_debug_events_poll(struct file *file, poll_table *wait)
{
	struct hid_debug_list *list = file->private_data;

	poll_wait(file, &list->hdev->debug_wait, wait);
	if (!kfifo_is_empty(&list->hid_debug_fifo))
		return EPOLLIN | EPOLLRDNORM;
	if (!list->hdev->debug)
		return EPOLLERR | EPOLLHUP;
	return 0;
}

static int hid_debug_events_release(struct inode *inode, struct file *file)
{
	struct hid_debug_list *list = file->private_data;
	unsigned long flags;

	spin_lock_irqsave(&list->hdev->debug_list_lock, flags);
	list_del(&list->node);
	spin_unlock_irqrestore(&list->hdev->debug_list_lock, flags);
	kfifo_free(&list->hid_debug_fifo);
	kfree(list);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(hid_debug_rdesc);

static const struct file_operations hid_debug_events_fops = {
	.owner =        THIS_MODULE,
	.open           = hid_debug_events_open,
	.read           = hid_debug_events_read,
	.poll		= hid_debug_events_poll,
	.release        = hid_debug_events_release,
	.llseek		= noop_llseek,
};


void hid_debug_register(struct hid_device *hdev, const char *name)
{
	hdev->debug_dir = debugfs_create_dir(name, hid_debug_root);
	hdev->debug_rdesc = debugfs_create_file("rdesc", 0400,
			hdev->debug_dir, hdev, &hid_debug_rdesc_fops);
	hdev->debug_events = debugfs_create_file("events", 0400,
			hdev->debug_dir, hdev, &hid_debug_events_fops);
	hdev->debug = 1;
}

void hid_debug_unregister(struct hid_device *hdev)
{
	hdev->debug = 0;
	wake_up_interruptible(&hdev->debug_wait);
	debugfs_remove(hdev->debug_rdesc);
	debugfs_remove(hdev->debug_events);
	debugfs_remove(hdev->debug_dir);
}

void hid_debug_init(void)
{
	hid_debug_root = debugfs_create_dir("hid", NULL);
}

void hid_debug_exit(void)
{
	debugfs_remove_recursive(hid_debug_root);
}
