/*
 *  arch/ppc/kernel/open_pic_defs.h -- OpenPIC definitions
 *
 *  Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is based on the following documentation:
 *
 *	The Open Programmable Interrupt Controller (PIC)
 *	Register Interface Specification Revision 1.2
 *
 *	Issue Date: October 1995
 *
 *	Issued jointly by Advanced Micro Devices and Cyrix Corporation
 *
 *	AMD is a registered trademark of Advanced Micro Devices, Inc.
 *	Copyright (C) 1995, Advanced Micro Devices, Inc. and Cyrix, Inc.
 *	All Rights Reserved.
 *
 *  To receive a copy of this documentation, send an email to openpic@amd.com.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _LINUX_OPENPIC_H
#define _LINUX_OPENPIC_H

#ifdef __KERNEL__

    /*
     *  OpenPIC supports up to 2048 interrupt sources and up to 32 processors
     */

#define OPENPIC_MAX_SOURCES	2048
#define OPENPIC_MAX_PROCESSORS	32
#define OPENPIC_MAX_ISU		16

#define OPENPIC_NUM_TIMERS	4
#define OPENPIC_NUM_IPI		4
#define OPENPIC_NUM_PRI		16
#define OPENPIC_NUM_VECTORS	256



    /*
     *  OpenPIC Registers are 32 bits and aligned on 128 bit boundaries
     */

typedef struct _OpenPIC_Reg {
    u_int Reg;					/* Little endian! */
    char Pad[0xc];
} OpenPIC_Reg;


    /*
     *  Per Processor Registers
     */

typedef struct _OpenPIC_Processor {
    /*
     *  Private Shadow Registers (for SLiC backwards compatibility)
     */
    u_int IPI0_Dispatch_Shadow;			/* Write Only */
    char Pad1[0x4];
    u_int IPI0_Vector_Priority_Shadow;		/* Read/Write */
    char Pad2[0x34];
    /*
     *  Interprocessor Interrupt Command Ports
     */
    OpenPIC_Reg _IPI_Dispatch[OPENPIC_NUM_IPI];	/* Write Only */
    /*
     *  Current Task Priority Register
     */
    OpenPIC_Reg _Current_Task_Priority;		/* Read/Write */
    char Pad3[0x10];
    /*
     *  Interrupt Acknowledge Register
     */
    OpenPIC_Reg _Interrupt_Acknowledge;		/* Read Only */
    /*
     *  End of Interrupt (EOI) Register
     */
    OpenPIC_Reg _EOI;				/* Read/Write */
    char Pad5[0xf40];
} OpenPIC_Processor;


    /*
     *  Timer Registers
     */

typedef struct _OpenPIC_Timer {
    OpenPIC_Reg _Current_Count;			/* Read Only */
    OpenPIC_Reg _Base_Count;			/* Read/Write */
    OpenPIC_Reg _Vector_Priority;		/* Read/Write */
    OpenPIC_Reg _Destination;			/* Read/Write */
} OpenPIC_Timer;


    /*
     *  Global Registers
     */

typedef struct _OpenPIC_Global {
    /*
     *  Feature Reporting Registers
     */
    OpenPIC_Reg _Feature_Reporting0;		/* Read Only */
    OpenPIC_Reg _Feature_Reporting1;		/* Future Expansion */
    /*
     *  Global Configuration Registers
     */
    OpenPIC_Reg _Global_Configuration0;		/* Read/Write */
    OpenPIC_Reg _Global_Configuration1;		/* Future Expansion */
    /*
     *  Vendor Specific Registers
     */
    OpenPIC_Reg _Vendor_Specific[4];
    /*
     *  Vendor Identification Register
     */
    OpenPIC_Reg _Vendor_Identification;		/* Read Only */
    /*
     *  Processor Initialization Register
     */
    OpenPIC_Reg _Processor_Initialization;	/* Read/Write */
    /*
     *  IPI Vector/Priority Registers
     */
    OpenPIC_Reg _IPI_Vector_Priority[OPENPIC_NUM_IPI];	/* Read/Write */
    /*
     *  Spurious Vector Register
     */
    OpenPIC_Reg _Spurious_Vector;		/* Read/Write */
    /*
     *  Global Timer Registers
     */
    OpenPIC_Reg _Timer_Frequency;		/* Read/Write */
    OpenPIC_Timer Timer[OPENPIC_NUM_TIMERS];
    char Pad1[0xee00];
} OpenPIC_Global;


    /*
     *  Interrupt Source Registers
     */

typedef struct _OpenPIC_Source {
    OpenPIC_Reg _Vector_Priority;		/* Read/Write */
    OpenPIC_Reg _Destination;			/* Read/Write */
} OpenPIC_Source, *OpenPIC_SourcePtr;


    /*
     *  OpenPIC Register Map
     */

struct OpenPIC {
    char Pad1[0x1000];
    /*
     *  Global Registers
     */
    OpenPIC_Global Global;
    /*
     *  Interrupt Source Configuration Registers
     */
    OpenPIC_Source Source[OPENPIC_MAX_SOURCES];
    /*
     *  Per Processor Registers
     */
    OpenPIC_Processor Processor[OPENPIC_MAX_PROCESSORS];
};

    /*
     *  Current Task Priority Register
     */

#define OPENPIC_CURRENT_TASK_PRIORITY_MASK	0x0000000f

    /*
     *  Who Am I Register
     */

#define OPENPIC_WHO_AM_I_ID_MASK		0x0000001f

    /*
     *  Feature Reporting Register 0
     */

#define OPENPIC_FEATURE_LAST_SOURCE_MASK	0x07ff0000
#define OPENPIC_FEATURE_LAST_SOURCE_SHIFT	16
#define OPENPIC_FEATURE_LAST_PROCESSOR_MASK	0x00001f00
#define OPENPIC_FEATURE_LAST_PROCESSOR_SHIFT	8
#define OPENPIC_FEATURE_VERSION_MASK		0x000000ff

    /*
     *  Global Configuration Register 0
     */

#define OPENPIC_CONFIG_RESET			0x80000000
#define OPENPIC_CONFIG_8259_PASSTHROUGH_DISABLE	0x20000000
#define OPENPIC_CONFIG_BASE_MASK		0x000fffff

    /*
     *  Global Configuration Register 1
     *  This is the EICR on EPICs.
     */

#define OPENPIC_EICR_S_CLK_MASK			0x70000000
#define OPENPIC_EICR_SIE			0x08000000

    /*
     *  Vendor Identification Register
     */

#define OPENPIC_VENDOR_ID_STEPPING_MASK		0x00ff0000
#define OPENPIC_VENDOR_ID_STEPPING_SHIFT	16
#define OPENPIC_VENDOR_ID_DEVICE_ID_MASK	0x0000ff00
#define OPENPIC_VENDOR_ID_DEVICE_ID_SHIFT	8
#define OPENPIC_VENDOR_ID_VENDOR_ID_MASK	0x000000ff

    /*
     *  Vector/Priority Registers
     */

#define OPENPIC_MASK				0x80000000
#define OPENPIC_ACTIVITY			0x40000000	/* Read Only */
#define OPENPIC_PRIORITY_MASK			0x000f0000
#define OPENPIC_PRIORITY_SHIFT			16
#define OPENPIC_VECTOR_MASK			0x000000ff


    /*
     *  Interrupt Source Registers
     */

#define OPENPIC_POLARITY_POSITIVE		0x00800000
#define OPENPIC_POLARITY_NEGATIVE		0x00000000
#define OPENPIC_POLARITY_MASK			0x00800000
#define OPENPIC_SENSE_LEVEL			0x00400000
#define OPENPIC_SENSE_EDGE			0x00000000
#define OPENPIC_SENSE_MASK			0x00400000


    /*
     *  Timer Registers
     */

#define OPENPIC_COUNT_MASK			0x7fffffff
#define OPENPIC_TIMER_TOGGLE			0x80000000
#define OPENPIC_TIMER_COUNT_INHIBIT		0x80000000


    /*
     *  Aliases to make life simpler
     */

/* Per Processor Registers */
#define IPI_Dispatch(i)			_IPI_Dispatch[i].Reg
#define Current_Task_Priority		_Current_Task_Priority.Reg
#define Interrupt_Acknowledge		_Interrupt_Acknowledge.Reg
#define EOI				_EOI.Reg

/* Global Registers */
#define Feature_Reporting0		_Feature_Reporting0.Reg
#define Feature_Reporting1		_Feature_Reporting1.Reg
#define Global_Configuration0		_Global_Configuration0.Reg
#define Global_Configuration1		_Global_Configuration1.Reg
#define Vendor_Specific(i)		_Vendor_Specific[i].Reg
#define Vendor_Identification		_Vendor_Identification.Reg
#define Processor_Initialization	_Processor_Initialization.Reg
#define IPI_Vector_Priority(i)		_IPI_Vector_Priority[i].Reg
#define Spurious_Vector			_Spurious_Vector.Reg
#define Timer_Frequency			_Timer_Frequency.Reg

/* Timer Registers */
#define Current_Count			_Current_Count.Reg
#define Base_Count			_Base_Count.Reg
#define Vector_Priority			_Vector_Priority.Reg
#define Destination			_Destination.Reg

/* Interrupt Source Registers */
#define Vector_Priority			_Vector_Priority.Reg
#define Destination			_Destination.Reg

#endif /* __KERNEL__ */

#endif /* _LINUX_OPENPIC_H */
