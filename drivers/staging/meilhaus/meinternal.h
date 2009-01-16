/*
 * Copyright (C) 2005 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * Source File : meinternal.h
 * Author      : GG (Guenter Gebhardt)  <g.gebhardt@meilhaus.de>
 */

#ifndef _MEINTERNAL_H_
#define _MEINTERNAL_H_

/*=============================================================================
  PCI Vendor IDs
  ===========================================================================*/

#define PCI_VENDOR_ID_MEILHAUS						0x1402

/*=============================================================================
  PCI Device IDs
  ===========================================================================*/

#define PCI_DEVICE_ID_MEILHAUS_ME1000				0x1000
#define PCI_DEVICE_ID_MEILHAUS_ME1000_A				0x100A
#define PCI_DEVICE_ID_MEILHAUS_ME1000_B				0x100B

#define PCI_DEVICE_ID_MEILHAUS_ME1400				0x1400
#define PCI_DEVICE_ID_MEILHAUS_ME140A				0x140A
#define PCI_DEVICE_ID_MEILHAUS_ME140B				0x140B
#define PCI_DEVICE_ID_MEILHAUS_ME14E0				0x14E0
#define PCI_DEVICE_ID_MEILHAUS_ME14EA				0x14EA
#define PCI_DEVICE_ID_MEILHAUS_ME14EB				0x14EB
#define PCI_DEVICE_ID_MEILHAUS_ME140C				0X140C
#define PCI_DEVICE_ID_MEILHAUS_ME140D				0X140D

#define PCI_DEVICE_ID_MEILHAUS_ME1600_4U			0x1604 // 4 voltage outputs
#define PCI_DEVICE_ID_MEILHAUS_ME1600_8U			0x1608 // 8 voltage outputs
#define PCI_DEVICE_ID_MEILHAUS_ME1600_12U			0x160C // 12 voltage outputs
#define PCI_DEVICE_ID_MEILHAUS_ME1600_16U			0x160F // 16 voltage outputs
#define PCI_DEVICE_ID_MEILHAUS_ME1600_16U_8I		0x168F // 16 voltage/8 current o.

#define PCI_DEVICE_ID_MEILHAUS_ME4610				0x4610 // Jekyll

#define PCI_DEVICE_ID_MEILHAUS_ME4650				0x4650 // Low Cost version

#define PCI_DEVICE_ID_MEILHAUS_ME4660				0x4660 // Standard version
#define PCI_DEVICE_ID_MEILHAUS_ME4660I				0x4661 // Isolated version
#define PCI_DEVICE_ID_MEILHAUS_ME4660S				0x4662 // Standard version with Sample and Hold
#define PCI_DEVICE_ID_MEILHAUS_ME4660IS				0x4663 // Isolated version with Sample and Hold

#define PCI_DEVICE_ID_MEILHAUS_ME4670				0x4670 // Standard version
#define PCI_DEVICE_ID_MEILHAUS_ME4670I				0x4671 // Isolated version
#define PCI_DEVICE_ID_MEILHAUS_ME4670S				0x4672 // Standard version with Sample and Hold
#define PCI_DEVICE_ID_MEILHAUS_ME4670IS				0x4673 // Isolated version with Sample and Hold

#define PCI_DEVICE_ID_MEILHAUS_ME4680				0x4680 // Standard version
#define PCI_DEVICE_ID_MEILHAUS_ME4680I				0x4681 // Isolated version
#define PCI_DEVICE_ID_MEILHAUS_ME4680S				0x4682 // Standard version with Sample and Hold
#define PCI_DEVICE_ID_MEILHAUS_ME4680IS				0x4683 // Isolated version with Sample and Hold

/* ME6000 standard version */
#define PCI_DEVICE_ID_MEILHAUS_ME6004   			0x6004
#define PCI_DEVICE_ID_MEILHAUS_ME6008   			0x6008
#define PCI_DEVICE_ID_MEILHAUS_ME600F   			0x600F

/* ME6000 isolated version */
#define PCI_DEVICE_ID_MEILHAUS_ME6014   			0x6014
#define PCI_DEVICE_ID_MEILHAUS_ME6018   			0x6018
#define PCI_DEVICE_ID_MEILHAUS_ME601F   			0x601F

/* ME6000 isle version */
#define PCI_DEVICE_ID_MEILHAUS_ME6034   			0x6034
#define PCI_DEVICE_ID_MEILHAUS_ME6038   			0x6038
#define PCI_DEVICE_ID_MEILHAUS_ME603F   			0x603F

/* ME6000 standard version with DIO */
#define PCI_DEVICE_ID_MEILHAUS_ME6044   			0x6044
#define PCI_DEVICE_ID_MEILHAUS_ME6048   			0x6048
#define PCI_DEVICE_ID_MEILHAUS_ME604F   			0x604F

/* ME6000 isolated version with DIO */
#define PCI_DEVICE_ID_MEILHAUS_ME6054   			0x6054
#define PCI_DEVICE_ID_MEILHAUS_ME6058   			0x6058
#define PCI_DEVICE_ID_MEILHAUS_ME605F   			0x605F

/* ME6000 isle version with DIO */
#define PCI_DEVICE_ID_MEILHAUS_ME6074   			0x6074
#define PCI_DEVICE_ID_MEILHAUS_ME6078   			0x6078
#define PCI_DEVICE_ID_MEILHAUS_ME607F   			0x607F

/* ME6100 standard version */
#define PCI_DEVICE_ID_MEILHAUS_ME6104   			0x6104
#define PCI_DEVICE_ID_MEILHAUS_ME6108   			0x6108
#define PCI_DEVICE_ID_MEILHAUS_ME610F   			0x610F

/* ME6100 isolated version */
#define PCI_DEVICE_ID_MEILHAUS_ME6114   			0x6114
#define PCI_DEVICE_ID_MEILHAUS_ME6118   			0x6118
#define PCI_DEVICE_ID_MEILHAUS_ME611F   			0x611F

/* ME6100 isle version */
#define PCI_DEVICE_ID_MEILHAUS_ME6134   			0x6134
#define PCI_DEVICE_ID_MEILHAUS_ME6138   			0x6138
#define PCI_DEVICE_ID_MEILHAUS_ME613F   			0x613F

/* ME6100 standard version with DIO */
#define PCI_DEVICE_ID_MEILHAUS_ME6144				0x6144
#define PCI_DEVICE_ID_MEILHAUS_ME6148   			0x6148
#define PCI_DEVICE_ID_MEILHAUS_ME614F   			0x614F

/* ME6100 isolated version with DIO */
#define PCI_DEVICE_ID_MEILHAUS_ME6154   			0x6154
#define PCI_DEVICE_ID_MEILHAUS_ME6158   			0x6158
#define PCI_DEVICE_ID_MEILHAUS_ME615F   			0x615F

/* ME6100 isle version with DIO */
#define PCI_DEVICE_ID_MEILHAUS_ME6174   			0x6174
#define PCI_DEVICE_ID_MEILHAUS_ME6178   			0x6178
#define PCI_DEVICE_ID_MEILHAUS_ME617F   			0x617F

/* ME6200 isolated version with DIO */
#define PCI_DEVICE_ID_MEILHAUS_ME6259				0x6259

/* ME6300 isolated version with DIO */
#define PCI_DEVICE_ID_MEILHAUS_ME6359				0x6359

/* ME0630 */
#define PCI_DEVICE_ID_MEILHAUS_ME0630				0x0630

/* ME8100 */
#define PCI_DEVICE_ID_MEILHAUS_ME8100_A				0x810A
#define PCI_DEVICE_ID_MEILHAUS_ME8100_B  			0x810B

/* ME8200 */
#define PCI_DEVICE_ID_MEILHAUS_ME8200_A				0x820A
#define PCI_DEVICE_ID_MEILHAUS_ME8200_B  			0x820B

/* ME0900 */
#define PCI_DEVICE_ID_MEILHAUS_ME0940				0x0940
#define PCI_DEVICE_ID_MEILHAUS_ME0950				0x0950
#define PCI_DEVICE_ID_MEILHAUS_ME0960				0x0960


/*=============================================================================
  USB Vendor IDs
  ===========================================================================*/

//#define USB_VENDOR_ID_MEPHISTO_S1					0x0403


/*=============================================================================
  USB Device IDs
  ===========================================================================*/

//#define USB_DEVICE_ID_MEPHISTO_S1					0xDCD0


/* ME-1000 defines */
#define ME1000_NAME_DRIVER							"ME-1000"

#define ME1000_NAME_DEVICE_ME1000					"ME-1000"

#define ME1000_DESCRIPTION_DEVICE_ME1000			"ME-1000 device, 128 digital i/o lines."

/* ME-1400 defines */
#define ME1400_NAME_DRIVER							"ME-1400"

#define ME1400_NAME_DEVICE_ME1400					"ME-1400"
#define ME1400_NAME_DEVICE_ME1400E					"ME-1400E"
#define ME1400_NAME_DEVICE_ME1400A					"ME-1400A"
#define ME1400_NAME_DEVICE_ME1400EA					"ME-1400EA"
#define ME1400_NAME_DEVICE_ME1400B					"ME-1400B"
#define ME1400_NAME_DEVICE_ME1400EB					"ME-1400EB"
#define ME1400_NAME_DEVICE_ME1400C					"ME-1400C"
#define ME1400_NAME_DEVICE_ME1400D					"ME-1400D"

#define ME1400_DESCRIPTION_DEVICE_ME1400			"ME-1400 device, 24 digital i/o lines."
#define ME1400_DESCRIPTION_DEVICE_ME1400E			"ME-1400E device, 24 digital i/o lines."
#define ME1400_DESCRIPTION_DEVICE_ME1400A			"ME-1400A device, 24 digital i/o lines, 3 counters."
#define ME1400_DESCRIPTION_DEVICE_ME1400EA			"ME-1400EA device, 24 digital i/o lines, 3 counters."
#define ME1400_DESCRIPTION_DEVICE_ME1400B			"ME-1400B device, 48 digital i/o lines, 6 counters."
#define ME1400_DESCRIPTION_DEVICE_ME1400EB			"ME-1400EB device, 48 digital i/o lines, 6 counters."
#define ME1400_DESCRIPTION_DEVICE_ME1400C			"ME-1400C device, 24 digital i/o lines, 15 counters."
#define ME1400_DESCRIPTION_DEVICE_ME1400D			"ME-1400D device, 48 digital i/o lines, 30 counters."

/* ME-1600 defines */
#define ME1600_NAME_DRIVER							"ME-1600"

#define ME1600_NAME_DEVICE_ME16004U					"ME-1600/4U"
#define ME1600_NAME_DEVICE_ME16008U					"ME-1600/8U"
#define ME1600_NAME_DEVICE_ME160012U				"ME-1600/12U"
#define ME1600_NAME_DEVICE_ME160016U				"ME-1600/16U"
#define ME1600_NAME_DEVICE_ME160016U8I				"ME-1600/16U8I"

#define ME1600_DESCRIPTION_DEVICE_ME16004U			"ME-1600/4U device, 4 voltage outputs."
#define ME1600_DESCRIPTION_DEVICE_ME16008U			"ME-1600/8U device, 8 voltage outputs."
#define ME1600_DESCRIPTION_DEVICE_ME160012U			"ME-1600/12U device, 12 voltage outputs."
#define ME1600_DESCRIPTION_DEVICE_ME160016U			"ME-1600/16U device, 16 voltage outputs."
#define ME1600_DESCRIPTION_DEVICE_ME160016U8I		"ME-1600/16U8I device, 16 voltage, 8 current outputs."

/* ME-4000 defines */
#define ME4600_NAME_DRIVER							"ME-4600"

#define ME4600_NAME_DEVICE_ME4610					"ME-4610"
#define ME4600_NAME_DEVICE_ME4650					"ME-4650"
#define ME4600_NAME_DEVICE_ME4660					"ME-4660"
#define ME4600_NAME_DEVICE_ME4660I					"ME-4660I"
#define ME4600_NAME_DEVICE_ME4660S					"ME-4660S"
#define ME4600_NAME_DEVICE_ME4660IS					"ME-4660IS"
#define ME4600_NAME_DEVICE_ME4670					"ME-4670"
#define ME4600_NAME_DEVICE_ME4670I					"ME-4670I"
#define ME4600_NAME_DEVICE_ME4670S					"ME-4670S"
#define ME4600_NAME_DEVICE_ME4670IS					"ME-4670IS"
#define ME4600_NAME_DEVICE_ME4680					"ME-4680"
#define ME4600_NAME_DEVICE_ME4680I					"ME-4680I"
#define ME4600_NAME_DEVICE_ME4680S					"ME-4680S"
#define ME4600_NAME_DEVICE_ME4680IS					"ME-4680IS"

#define ME4600_DESCRIPTION_DEVICE_ME4610			"ME-4610 device, 16 streaming analog inputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4650			"ME-4650 device, 16 streaming analog inputs, 32 digital i/o lines, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4660			"ME-4660 device, 16 streaming analog inputs, 2 single analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4660I			"ME-4660I opto isolated device, 16 streaming analog inputs, 2 single analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4660S			"ME-4660 device, 16 streaming analog inputs (8 S&H), 2 single analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4660IS			"ME-4660I opto isolated device, 16 streaming analog inputs (8 S&H), 2 single analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4670			"ME-4670 device, 32 streaming analog inputs, 4 single analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4670I			"ME-4670I opto isolated device, 32 streaming analog inputs, 4 single analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4670S			"ME-4670S device, 32 streaming analog inputs (8 S&H), 4 single analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4670IS			"ME-4670IS opto isolated device, 32 streaming analog inputs (8 S&H), 4 single analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4680			"ME-4680 device, 32 streaming analog inputs, 4 streaming analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4680I			"ME-4680I opto isolated device, 32 streaming analog inputs, 4 streaming analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4680S			"ME-4680S device, 32 streaming analog inputs, 4 streaming analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."
#define ME4600_DESCRIPTION_DEVICE_ME4680IS			"ME-4680IS opto isolated device, 32 streaming analog inputs (8 S&H), 4 streaming analog outputs, 32 digital i/o lines, 3 counters, 1 external interrupt."

/* ME-6000 defines */
#define ME6000_NAME_DRIVER							"ME-6000"

#define ME6000_NAME_DEVICE_ME60004					"ME-6000/4"
#define ME6000_NAME_DEVICE_ME60008					"ME-6000/8"
#define ME6000_NAME_DEVICE_ME600016					"ME-6000/16"
#define ME6000_NAME_DEVICE_ME6000I4					"ME-6000I/4"
#define ME6000_NAME_DEVICE_ME6000I8					"ME-6000I/8"
#define ME6000_NAME_DEVICE_ME6000I16				"ME-6000I/16"
#define ME6000_NAME_DEVICE_ME6000ISLE4				"ME-6000ISLE/4"
#define ME6000_NAME_DEVICE_ME6000ISLE8				"ME-6000ISLE/8"
#define ME6000_NAME_DEVICE_ME6000ISLE16				"ME-6000ISLE/16"
#define ME6000_NAME_DEVICE_ME61004					"ME-6100/4"
#define ME6000_NAME_DEVICE_ME61008					"ME-6100/8"
#define ME6000_NAME_DEVICE_ME610016					"ME-6100/16"
#define ME6000_NAME_DEVICE_ME6100I4					"ME-6100I/4"
#define ME6000_NAME_DEVICE_ME6100I8					"ME-6100I/8"
#define ME6000_NAME_DEVICE_ME6100I16				"ME-6100I/16"
#define ME6000_NAME_DEVICE_ME6100ISLE4				"ME-6100ISLE/4"
#define ME6000_NAME_DEVICE_ME6100ISLE8				"ME-6100ISLE/8"
#define ME6000_NAME_DEVICE_ME6100ISLE16				"ME-6100ISLE/16"
#define ME6000_NAME_DEVICE_ME60004DIO				"ME-6000/4/DIO"
#define ME6000_NAME_DEVICE_ME60008DIO				"ME-6000/8/DIO"
#define ME6000_NAME_DEVICE_ME600016DIO				"ME-6000/16/DIO"
#define ME6000_NAME_DEVICE_ME6000I4DIO				"ME-6000I/4/DIO"
#define ME6000_NAME_DEVICE_ME6000I8DIO				"ME-6000I/8/DIO"
#define ME6000_NAME_DEVICE_ME6000I16DIO				"ME-6000I/16/DIO"
#define ME6000_NAME_DEVICE_ME6000ISLE4DIO			"ME-6000ISLE/4/DIO"
#define ME6000_NAME_DEVICE_ME6000ISLE8DIO			"ME-6000ISLE/8/DIO"
#define ME6000_NAME_DEVICE_ME6000ISLE16DIO			"ME-6000ISLE/16/DIO"
#define ME6000_NAME_DEVICE_ME61004DIO				"ME-6100/4/DIO"
#define ME6000_NAME_DEVICE_ME61008DIO				"ME-6100/8/DIO"
#define ME6000_NAME_DEVICE_ME610016DIO				"ME-6100/16/DIO"
#define ME6000_NAME_DEVICE_ME6100I4DIO				"ME-6100I/4/DIO"
#define ME6000_NAME_DEVICE_ME6100I8DIO				"ME-6100I/8/DIO"
#define ME6000_NAME_DEVICE_ME6100I16DIO				"ME-6100I/16/DIO"
#define ME6000_NAME_DEVICE_ME6100ISLE4DIO			"ME-6100ISLE/4/DIO"
#define ME6000_NAME_DEVICE_ME6100ISLE8DIO			"ME-6100ISLE/8/DIO"
#define ME6000_NAME_DEVICE_ME6100ISLE16DIO			"ME-6100ISLE/16/DIO"
#define ME6000_NAME_DEVICE_ME6200I9DIO				"ME-6200I/9/DIO"
#define ME6000_NAME_DEVICE_ME6300I9DIO				"ME-6300I/9/DIO"

#define ME6000_DESCRIPTION_DEVICE_ME60004			"ME-6000/4 device, 4 single analog outputs."
#define ME6000_DESCRIPTION_DEVICE_ME60008			"ME-6000/8 device, 8 single analog outputs"
#define ME6000_DESCRIPTION_DEVICE_ME600016			"ME-6000/16 device, 16 single analog outputs"
#define ME6000_DESCRIPTION_DEVICE_ME6000I4			"ME-6000I/4 isolated device, 4 single analog outputs"
#define ME6000_DESCRIPTION_DEVICE_ME6000I8			"ME-6000I/8 isolated device, 8 single analog outputs"
#define ME6000_DESCRIPTION_DEVICE_ME6000I16			"ME-6000I/16 isolated device, 16 single analog outputs"
#define ME6000_DESCRIPTION_DEVICE_ME6000ISLE4		"ME-6000ISLE/4 isle device, 4 single analog outputs"
#define ME6000_DESCRIPTION_DEVICE_ME6000ISLE8		"ME-6000ISLE/8 isle device, 8 single analog outputs"
#define ME6000_DESCRIPTION_DEVICE_ME6000ISLE16		"ME-6000ISLE/16 isle device, 16 single analog outputs"
#define ME6000_DESCRIPTION_DEVICE_ME61004			"ME-6100/4 device, 4 streaming analog outputs."
#define ME6000_DESCRIPTION_DEVICE_ME61008			"ME-6100/8 device, 4 streaming, 4 single analog outputs."
#define ME6000_DESCRIPTION_DEVICE_ME610016			"ME-6100/16 device, 4 streaming, 12 single analog outputs."
#define ME6000_DESCRIPTION_DEVICE_ME6100I4			"ME-6100I/4 isolated device, 4 streaming analog outputs."
#define ME6000_DESCRIPTION_DEVICE_ME6100I8			"ME-6100I/8 isolated device, 4 streaming, 4 single analog outputs."
#define ME6000_DESCRIPTION_DEVICE_ME6100I16			"ME-6100I/16 isolated device, 4 streaming, 12 single analog outputs."
#define ME6000_DESCRIPTION_DEVICE_ME6100ISLE4		"ME-6100ISLE/4 isle device, 4 streaming analog outputs."
#define ME6000_DESCRIPTION_DEVICE_ME6100ISLE8		"ME-6100ISLE/8 isle device, 4 streaming, 4 single analog outputs."
#define ME6000_DESCRIPTION_DEVICE_ME6100ISLE16		"ME-6100ISLE/16 isle device, 4 streaming, 12 single analog outputs."
#define ME6000_DESCRIPTION_DEVICE_ME60004DIO		"ME-6000/4/DIO device, 4 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME60008DIO		"ME-6000/8/DIO device, 8 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME600016DIO		"ME-6000/16/DIO device, 8 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6000I4DIO		"ME-6000I/4/DIO isolated device, 4 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6000I8DIO		"ME-6000I/8/DIO isolated device, 8 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6000I16DIO		"ME-6000I/16/DIO isolated device, 16 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6000ISLE4DIO	"ME-6000ISLE/4/DIO isle device, 4 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6000ISLE8DIO	"ME-6000ISLE/8/DIO isle device, 8 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6000ISLE16DIO	"ME-6000ISLE/16/DIO isle device, 16 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME61004DIO		"ME-6100/4/DIO device, 4 streaming analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME61008DIO		"ME-6100/8/DIO device, 4 streaming, 4 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME610016DIO		"ME-6100/16/DIO device, 4 streaming, 12 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6100I4DIO		"ME-6100I/4/DIO isolated device, 4 streaming analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6100I8DIO		"ME-6100I/8/DIO isolated device, 4 streaming, 4 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6100I16DIO		"ME-6100I/16/DIO isolated device, 4 streaming, 12 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6100ISLE4DIO	"ME-6100ISLE/4/DIO isle device, 4 streaming analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6100ISLE8DIO	"ME-6100ISLE/8/DIO isle device, 4 streaming, 4 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6100ISLE16DIO	"ME-6100ISLE/16/DIO isle device, 4 streaming, 12 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6200I9DIO		"ME-6200I/9/DIO isolated device, 9 single analog outputs, 16 digital i/o lines."
#define ME6000_DESCRIPTION_DEVICE_ME6300I9DIO		"ME-6300I/9/DIO isolated device, 4 streaming, 5 single analog outputs, 16 digital i/o lines."

/* ME-630 defines */
#define ME0600_NAME_DRIVER							"ME-0600"

#define ME0600_NAME_DEVICE_ME0630					"ME-630"

#define ME0600_DESCRIPTION_DEVICE_ME0630			"ME-630 device, up to 16 relay, 8 digital ttl input lines, 8 isolated digital input lines, 16 digital i/o lines, 2 external interrupts."

/* ME-8100 defines */
#define ME8100_NAME_DRIVER							"ME-8100"

#define ME8100_NAME_DEVICE_ME8100A					"ME-8100A"
#define ME8100_NAME_DEVICE_ME8100B					"ME-8100B"

#define ME8100_DESCRIPTION_DEVICE_ME8100A			"ME-8100A opto isolated device, 16 digital input lines, 16 digital output lines."
#define ME8100_DESCRIPTION_DEVICE_ME8100B			"ME-8100B opto isolated device, 32 digital input lines, 32 digital output lines, 3 counters."

/* ME-8200 defines */
#define ME8200_NAME_DRIVER							"ME-8200"

#define ME8200_NAME_DEVICE_ME8200A					"ME-8200A"
#define ME8200_NAME_DEVICE_ME8200B					"ME-8200B"

#define ME8200_DESCRIPTION_DEVICE_ME8200A			"ME-8200A opto isolated device, 8 digital output lines, 8 digital input lines, 16 digital i/o lines."
#define ME8200_DESCRIPTION_DEVICE_ME8200B			"ME-8200B opto isolated device, 16 digital output lines, 16 digital input lines, 16 digital i/o lines."

/* ME-0900 defines */
#define ME0900_NAME_DRIVER							"ME-0900"

#define ME0900_NAME_DEVICE_ME0940					"ME-94"
#define ME0900_NAME_DEVICE_ME0950					"ME-95"
#define ME0900_NAME_DEVICE_ME0960					"ME-96"

#define ME0900_DESCRIPTION_DEVICE_ME0940			"ME-94 device, 16 digital input lines, 2 external interrupt lines."
#define ME0900_DESCRIPTION_DEVICE_ME0950			"ME-95 device, 16 digital output lines."
#define ME0900_DESCRIPTION_DEVICE_ME0960			"ME-96 device, 8 digital input lines, 8 digital output lines, 2 external interrupt lines."

/* ME-DUMMY defines */
#define MEDUMMY_NAME_DRIVER							"ME-Dummy"

/* MEPHISTO_S1 defines */
/*
#define MEPHISTO_S1_NAME_DRIVER						"MEphisto Scope 1"
#define MEPHISTO_S1_NAME_DEVICE						"MEphisto Scope 1"
#define MEPHISTO_S1_DESCRIPTION_DEVICE				"MEphisto Scope 1 device, 2 analog inputs, 24 digital i/o."
*/
/* Error defines */
#define EMPTY_NAME_DRIVER							"ME-???"
#define EMPTY_NAME_DEVICE							"ME-???"
#define EMPTY_DESCRIPTION_DEVICE					"ME-??? unknown device"

#endif
