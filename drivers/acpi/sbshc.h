struct acpi_smb_hc;
enum acpi_smb_protocol {
	SMBUS_WRITE_QUICK = 2,
	SMBUS_READ_QUICK = 3,
	SMBUS_SEND_BYTE = 4,
	SMBUS_RECEIVE_BYTE = 5,
	SMBUS_WRITE_BYTE = 6,
	SMBUS_READ_BYTE = 7,
	SMBUS_WRITE_WORD  = 8,
	SMBUS_READ_WORD  = 9,
	SMBUS_WRITE_BLOCK = 0xa,
	SMBUS_READ_BLOCK = 0xb,
	SMBUS_PROCESS_CALL = 0xc,
	SMBUS_BLOCK_PROCESS_CALL = 0xd,
};

static const u8 SMBUS_PEC = 0x80;

typedef void (*smbus_alarm_callback)(void *context);

extern int acpi_smbus_read(struct acpi_smb_hc *hc, u8 protocol, u8 address,
	       u8 command, u8 * data);
extern int acpi_smbus_write(struct acpi_smb_hc *hc, u8 protocol, u8 slave_address,
		u8 command, u8 * data, u8 length);
extern int acpi_smbus_register_callback(struct acpi_smb_hc *hc,
			         smbus_alarm_callback callback, void *context);
extern int acpi_smbus_unregister_callback(struct acpi_smb_hc *hc);
