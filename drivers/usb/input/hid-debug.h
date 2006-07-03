/*
 * $Id: hid-debug.h,v 1.8 2001/09/25 09:37:57 vojtech Exp $
 *
 *  (c) 1999 Andreas Gal		<gal@cs.uni-magdeburg.de>
 *  (c) 2000-2001 Vojtech Pavlik	<vojtech@ucw.cz>
 *
 *  Some debug stuff for the HID parser.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/input.h>

struct hid_usage_entry {
	unsigned  page;
	unsigned  usage;
	char     *description;
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
    {0, 0x20, "Stylus"},
    {0, 0x21, "Puck"},
    {0, 0x22, "Finger"},
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
    { 0x85, 0x66, "RemainingCapacity" },
    { 0x85, 0x68, "RunTimeToEmpty" },
    { 0x85, 0x6a, "AverageTimeToFull" },
    { 0x85, 0x83, "DesignCapacity" },
    { 0x85, 0x85, "ManufacturerDate" },
    { 0x85, 0x89, "iDeviceChemistry" },
    { 0x85, 0x8b, "Rechargable" },
    { 0x85, 0x8f, "iOEMInformation" },
    { 0x85, 0x8d, "CapacityGranularity1" },
    { 0x85, 0xd0, "ACPresent" },
  /* pages 0xff00 to 0xffff are vendor-specific */
  { 0xffff, 0, "Vendor-specific-FF" },
  { 0, 0, NULL }
};

static void resolv_usage_page(unsigned page) {
	const struct hid_usage_entry *p;

	for (p = hid_usage_table; p->description; p++)
		if (p->page == page) {
			printk("%s", p->description);
			return;
		}
	printk("%04x", page);
}

static void resolv_usage(unsigned usage) {
	const struct hid_usage_entry *p;

	resolv_usage_page(usage >> 16);
	printk(".");
	for (p = hid_usage_table; p->description; p++)
		if (p->page == (usage >> 16)) {
			for(++p; p->description && p->usage != 0; p++)
				if (p->usage == (usage & 0xffff)) {
					printk("%s", p->description);
					return;
				}
			break;
		}
	printk("%04x", usage & 0xffff);
}

__inline__ static void tab(int n) {
	while (n--) printk(" ");
}

static void hid_dump_field(struct hid_field *field, int n) {
	int j;

	if (field->physical) {
		tab(n);
		printk("Physical(");
		resolv_usage(field->physical); printk(")\n");
	}
	if (field->logical) {
		tab(n);
		printk("Logical(");
		resolv_usage(field->logical); printk(")\n");
	}
	tab(n); printk("Usage(%d)\n", field->maxusage);
	for (j = 0; j < field->maxusage; j++) {
		tab(n+2);resolv_usage(field->usage[j].hid); printk("\n");
	}
	if (field->logical_minimum != field->logical_maximum) {
		tab(n); printk("Logical Minimum(%d)\n", field->logical_minimum);
		tab(n); printk("Logical Maximum(%d)\n", field->logical_maximum);
	}
	if (field->physical_minimum != field->physical_maximum) {
		tab(n); printk("Physical Minimum(%d)\n", field->physical_minimum);
		tab(n); printk("Physical Maximum(%d)\n", field->physical_maximum);
	}
	if (field->unit_exponent) {
		tab(n); printk("Unit Exponent(%d)\n", field->unit_exponent);
	}
	if (field->unit) {
		char *systems[5] = { "None", "SI Linear", "SI Rotation", "English Linear", "English Rotation" };
		char *units[5][8] = {
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
			tab(n); printk("Unit(Invalid)\n");
		}
		else {
			int earlier_unit = 0;

			tab(n); printk("Unit(%s : ", systems[sys]);

			for (i=1 ; i<sizeof(__u32)*2 ; i++) {
				char nibble = data & 0xf;
				data >>= 4;
				if (nibble != 0) {
					if(earlier_unit++ > 0)
						printk("*");
					printk("%s", units[sys][i]);
					if(nibble != 1) {
						/* This is a _signed_ nibble(!) */

						int val = nibble & 0x7;
						if(nibble & 0x08)
							val = -((0x7 & ~val) +1);
						printk("^%d", val);
					}
				}
			}
			printk(")\n");
		}
	}
	tab(n); printk("Report Size(%u)\n", field->report_size);
	tab(n); printk("Report Count(%u)\n", field->report_count);
	tab(n); printk("Report Offset(%u)\n", field->report_offset);

	tab(n); printk("Flags( ");
	j = field->flags;
	printk("%s", HID_MAIN_ITEM_CONSTANT & j ? "Constant " : "");
	printk("%s", HID_MAIN_ITEM_VARIABLE & j ? "Variable " : "Array ");
	printk("%s", HID_MAIN_ITEM_RELATIVE & j ? "Relative " : "Absolute ");
	printk("%s", HID_MAIN_ITEM_WRAP & j ? "Wrap " : "");
	printk("%s", HID_MAIN_ITEM_NONLINEAR & j ? "NonLinear " : "");
	printk("%s", HID_MAIN_ITEM_NO_PREFERRED & j ? "NoPrefferedState " : "");
	printk("%s", HID_MAIN_ITEM_NULL_STATE & j ? "NullState " : "");
	printk("%s", HID_MAIN_ITEM_VOLATILE & j ? "Volatile " : "");
	printk("%s", HID_MAIN_ITEM_BUFFERED_BYTE & j ? "BufferedByte " : "");
	printk(")\n");
}

static void __attribute__((unused)) hid_dump_device(struct hid_device *device) {
	struct hid_report_enum *report_enum;
	struct hid_report *report;
	struct list_head *list;
	unsigned i,k;
	static char *table[] = {"INPUT", "OUTPUT", "FEATURE"};

	for (i = 0; i < HID_REPORT_TYPES; i++) {
		report_enum = device->report_enum + i;
		list = report_enum->report_list.next;
		while (list != &report_enum->report_list) {
			report = (struct hid_report *) list;
			tab(2);
			printk("%s", table[i]);
			if (report->id)
				printk("(%d)", report->id);
			printk("[%s]", table[report->type]);
			printk("\n");
			for (k = 0; k < report->maxfield; k++) {
				tab(4);
				printk("Field(%d)\n", k);
				hid_dump_field(report->field[k], 6);
			}
			list = list->next;
		}
	}
}

static void __attribute__((unused)) hid_dump_input(struct hid_usage *usage, __s32 value) {
	printk("hid-debug: input ");
	resolv_usage(usage->hid);
	printk(" = %d\n", value);
}


static char *events[EV_MAX + 1] = {
	[EV_SYN] = "Sync",			[EV_KEY] = "Key",
	[EV_REL] = "Relative",			[EV_ABS] = "Absolute",
	[EV_MSC] = "Misc",			[EV_LED] = "LED",
	[EV_SND] = "Sound",			[EV_REP] = "Repeat",
	[EV_FF] = "ForceFeedback",		[EV_PWR] = "Power",
	[EV_FF_STATUS] = "ForceFeedbackStatus",
};

static char *syncs[2] = {
	[SYN_REPORT] = "Report",		[SYN_CONFIG] = "Config",
};
static char *keys[KEY_MAX + 1] = {
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
	[KEY_COFFEE] = "Coffee",		[KEY_DIRECTION] = "Direction",
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
	[BTN_TOOL_TRIPLETAP] = "ToolTripleTap", [BTN_GEAR_DOWN] = "WheelBtn",
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
	[KEY_DOCUMENTS] = "Documents",
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
};

static char *relatives[REL_MAX + 1] = {
	[REL_X] = "X",			[REL_Y] = "Y",
	[REL_Z] = "Z",			[REL_HWHEEL] = "HWheel",
	[REL_DIAL] = "Dial",		[REL_WHEEL] = "Wheel",
	[REL_MISC] = "Misc",
};

static char *absolutes[ABS_MAX + 1] = {
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
	[ABS_TILT_Y] = "YTilt",		[ABS_TOOL_WIDTH] = "Tool Width",
	[ABS_VOLUME] = "Volume",	[ABS_MISC] = "Misc",
};

static char *misc[MSC_MAX + 1] = {
	[MSC_SERIAL] = "Serial",	[MSC_PULSELED] = "Pulseled",
	[MSC_GESTURE] = "Gesture",	[MSC_RAW] = "RawData"
};

static char *leds[LED_MAX + 1] = {
	[LED_NUML] = "NumLock",		[LED_CAPSL] = "CapsLock",
	[LED_SCROLLL] = "ScrollLock",	[LED_COMPOSE] = "Compose",
	[LED_KANA] = "Kana",		[LED_SLEEP] = "Sleep",
	[LED_SUSPEND] = "Suspend",	[LED_MUTE] = "Mute",
	[LED_MISC] = "Misc",
};

static char *repeats[REP_MAX + 1] = {
	[REP_DELAY] = "Delay",		[REP_PERIOD] = "Period"
};

static char *sounds[SND_MAX + 1] = {
	[SND_CLICK] = "Click",		[SND_BELL] = "Bell",
	[SND_TONE] = "Tone"
};

static char **names[EV_MAX + 1] = {
	[EV_SYN] = syncs,			[EV_KEY] = keys,
	[EV_REL] = relatives,			[EV_ABS] = absolutes,
	[EV_MSC] = misc,			[EV_LED] = leds,
	[EV_SND] = sounds,			[EV_REP] = repeats,
};

static void __attribute__((unused)) resolv_event(__u8 type, __u16 code) {

	printk("%s.%s", events[type] ? events[type] : "?",
		names[type] ? (names[type][code] ? names[type][code] : "?") : "?");
}
