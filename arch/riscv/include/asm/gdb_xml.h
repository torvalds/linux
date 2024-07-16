/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_GDB_XML_H_
#define __ASM_GDB_XML_H_

const char riscv_gdb_stub_feature[64] =
			"PacketSize=800;qXfer:features:read+;";

static const char gdb_xfer_read_target[31] = "qXfer:features:read:target.xml:";

#ifdef CONFIG_64BIT
static const char gdb_xfer_read_cpuxml[39] =
			"qXfer:features:read:riscv-64bit-cpu.xml";

static const char riscv_gdb_stub_target_desc[256] =
"l<?xml version=\"1.0\"?>"
"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
"<target>"
"<xi:include href=\"riscv-64bit-cpu.xml\"/>"
"</target>";

static const char riscv_gdb_stub_cpuxml[2048] =
"l<?xml version=\"1.0\"?>"
"<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">"
"<feature name=\"org.gnu.gdb.riscv.cpu\">"
"<reg name=\""DBG_REG_ZERO"\" bitsize=\"64\" type=\"int\" regnum=\"0\"/>"
"<reg name=\""DBG_REG_RA"\" bitsize=\"64\" type=\"code_ptr\"/>"
"<reg name=\""DBG_REG_SP"\" bitsize=\"64\" type=\"data_ptr\"/>"
"<reg name=\""DBG_REG_GP"\" bitsize=\"64\" type=\"data_ptr\"/>"
"<reg name=\""DBG_REG_TP"\" bitsize=\"64\" type=\"data_ptr\"/>"
"<reg name=\""DBG_REG_T0"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_T1"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_T2"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_FP"\" bitsize=\"64\" type=\"data_ptr\"/>"
"<reg name=\""DBG_REG_S1"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_A0"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_A1"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_A2"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_A3"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_A4"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_A5"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_A6"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_A7"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_S2"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_S3"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_S4"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_S5"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_S6"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_S7"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_S8"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_S9"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_S10"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_S11"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_T3"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_T4"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_T5"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_T6"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_EPC"\" bitsize=\"64\" type=\"code_ptr\"/>"
"<reg name=\""DBG_REG_STATUS"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_BADADDR"\" bitsize=\"64\" type=\"int\"/>"
"<reg name=\""DBG_REG_CAUSE"\" bitsize=\"64\" type=\"int\"/>"
"</feature>";
#else
static const char gdb_xfer_read_cpuxml[39] =
			"qXfer:features:read:riscv-32bit-cpu.xml";

static const char riscv_gdb_stub_target_desc[256] =
"l<?xml version=\"1.0\"?>"
"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
"<target>"
"<xi:include href=\"riscv-32bit-cpu.xml\"/>"
"</target>";

static const char riscv_gdb_stub_cpuxml[2048] =
"l<?xml version=\"1.0\"?>"
"<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">"
"<feature name=\"org.gnu.gdb.riscv.cpu\">"
"<reg name=\""DBG_REG_ZERO"\" bitsize=\"32\" type=\"int\" regnum=\"0\"/>"
"<reg name=\""DBG_REG_RA"\" bitsize=\"32\" type=\"code_ptr\"/>"
"<reg name=\""DBG_REG_SP"\" bitsize=\"32\" type=\"data_ptr\"/>"
"<reg name=\""DBG_REG_GP"\" bitsize=\"32\" type=\"data_ptr\"/>"
"<reg name=\""DBG_REG_TP"\" bitsize=\"32\" type=\"data_ptr\"/>"
"<reg name=\""DBG_REG_T0"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_T1"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_T2"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_FP"\" bitsize=\"32\" type=\"data_ptr\"/>"
"<reg name=\""DBG_REG_S1"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_A0"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_A1"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_A2"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_A3"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_A4"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_A5"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_A6"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_A7"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_S2"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_S3"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_S4"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_S5"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_S6"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_S7"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_S8"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_S9"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_S10"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_S11"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_T3"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_T4"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_T5"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_T6"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_EPC"\" bitsize=\"32\" type=\"code_ptr\"/>"
"<reg name=\""DBG_REG_STATUS"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_BADADDR"\" bitsize=\"32\" type=\"int\"/>"
"<reg name=\""DBG_REG_CAUSE"\" bitsize=\"32\" type=\"int\"/>"
"</feature>";
#endif
#endif
