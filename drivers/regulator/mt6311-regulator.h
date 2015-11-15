/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Henry Chen <henryc.chen@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MT6311_REGULATOR_H__
#define __MT6311_REGULATOR_H__

#define MT6311_SWCID              0x01

#define MT6311_TOP_INT_CON        0x18
#define MT6311_TOP_INT_MON        0x19

#define MT6311_VDVFS11_CON0       0x87
#define MT6311_VDVFS11_CON7       0x88
#define MT6311_VDVFS11_CON8       0x89
#define MT6311_VDVFS11_CON9       0x8A
#define MT6311_VDVFS11_CON10      0x8B
#define MT6311_VDVFS11_CON11      0x8C
#define MT6311_VDVFS11_CON12      0x8D
#define MT6311_VDVFS11_CON13      0x8E
#define MT6311_VDVFS11_CON14      0x8F
#define MT6311_VDVFS11_CON15      0x90
#define MT6311_VDVFS11_CON16      0x91
#define MT6311_VDVFS11_CON17      0x92
#define MT6311_VDVFS11_CON18      0x93
#define MT6311_VDVFS11_CON19      0x94

#define MT6311_LDO_CON0           0xCC
#define MT6311_LDO_OCFB0          0xCD
#define MT6311_LDO_CON2           0xCE
#define MT6311_LDO_CON3           0xCF
#define MT6311_LDO_CON4           0xD0
#define MT6311_FQMTR_CON0         0xD1
#define MT6311_FQMTR_CON1         0xD2
#define MT6311_FQMTR_CON2         0xD3
#define MT6311_FQMTR_CON3         0xD4
#define MT6311_FQMTR_CON4         0xD5

#define MT6311_PMIC_RG_INT_POL_MASK                      0x1
#define MT6311_PMIC_RG_INT_EN_MASK                       0x2
#define MT6311_PMIC_RG_BUCK_OC_INT_STATUS_MASK           0x10

#define MT6311_PMIC_VDVFS11_EN_CTRL_MASK                 0x1
#define MT6311_PMIC_VDVFS11_VOSEL_CTRL_MASK              0x2
#define MT6311_PMIC_VDVFS11_EN_SEL_MASK                  0x3
#define MT6311_PMIC_VDVFS11_VOSEL_SEL_MASK               0xc
#define MT6311_PMIC_VDVFS11_EN_MASK                      0x1
#define MT6311_PMIC_VDVFS11_VOSEL_MASK                   0x7F
#define MT6311_PMIC_VDVFS11_VOSEL_ON_MASK                0x7F
#define MT6311_PMIC_VDVFS11_VOSEL_SLEEP_MASK             0x7F
#define MT6311_PMIC_NI_VDVFS11_VOSEL_MASK                0x7F

#define MT6311_PMIC_RG_VBIASN_EN_MASK                    0x1

#endif
