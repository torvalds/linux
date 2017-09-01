/*
 * Access to message bus through three registers
 * in CUNIT(0:0:0) PCI configuration space.
 * MSGBUS_CTRL_REG(0xD0):
 *   31:24      = message bus opcode
 *   23:16      = message bus port
 *   15:8       = message bus address, low 8 bits.
 *   7:4        = message bus byte enables
 * MSGBUS_CTRL_EXT_REG(0xD8):
 *   31:8       = message bus address, high 24 bits.
 * MSGBUS_DATA_REG(0xD4):
 *   hold the data for write or read
 */
#define PCI_ROOT_MSGBUS_CTRL_REG        0xD0
#define PCI_ROOT_MSGBUS_DATA_REG        0xD4
#define PCI_ROOT_MSGBUS_CTRL_EXT_REG    0xD8
#define PCI_ROOT_MSGBUS_READ            0x10
#define PCI_ROOT_MSGBUS_WRITE           0x11
#define PCI_ROOT_MSGBUS_DWORD_ENABLE    0xf0

u32 intel_mid_msgbus_read32_raw(u32 cmd);
u32 intel_mid_msgbus_read32(u8 port, u32 addr);
void intel_mid_msgbus_write32_raw(u32 cmd, u32 data);
void intel_mid_msgbus_write32(u8 port, u32 addr, u32 data);
u32 intel_mid_msgbus_read32_raw_ext(u32 cmd, u32 cmd_ext);
void intel_mid_msgbus_write32_raw_ext(u32 cmd, u32 cmd_ext, u32 data);
