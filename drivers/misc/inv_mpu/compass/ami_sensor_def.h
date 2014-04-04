/*
 * Copyright (C) 2010 Information System Products Co.,Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Definitions for ami306 compass chip.
 */
#ifndef AMI_SENSOR_DEF_H
#define AMI_SENSOR_DEF_H

/*********************************************************************
 Constant
 *********************************************************************/
#define	AMI_OK		0x00			/**< Normal */
#define	AMI_PARAM_ERR	0x01			/**< Parameter Error  */
#define	AMI_SEQ_ERR	0x02			/**< Squence Error  */
#define	AMI_SYSTEM_ERR	0x10			/**< System Error  */
#define AMI_BLOCK_ERR	0x20			/**< Block Error */
#define	AMI_ERROR	0x99			/**< other Error  */

/*********************************************************************
 Struct definition
 *********************************************************************/
/** axis sensitivity(gain) calibration parameter information  */
struct ami_vector3d {
	signed short x;			/**< X-axis  */
	signed short y;			/**< Y-axis  */
	signed short z;			/**< Z-axis  */
};

/** axis interference information  */
struct ami_interference {
	/**< Y-axis magnetic field for X-axis correction value  */
	signed short xy;
	/**< Z-axis magnetic field for X-axis correction value  */
	signed short xz;
	/**< X-axis magnetic field for Y-axis correction value  */
	signed short yx;
	/**< Z-axis magnetic field for Y-axis correction value  */
	signed short yz;
	/**< X-axis magnetic field for Z-axis correction value  */
	signed short zx;
	/**< Y-axis magnetic field for Z-axis correction value  */
	signed short zy;
};

/** sensor calibration Parameter information  */
struct ami_sensor_parametor {
	/**< geomagnetic field sensor gain  */
	struct ami_vector3d m_gain;
	/**< geomagnetic field sensor gain correction parameter  */
	struct ami_vector3d m_gain_cor;
	/**< geomagnetic field sensor offset  */
	struct ami_vector3d m_offset;
	/**< geomagnetic field sensor axis interference parameter */
	struct ami_interference m_interference;
#ifdef AMI_6AXIS
	/**< acceleration sensor gain  */
	struct ami_vector3d a_gain;
	/**< acceleration sensor offset  */
	struct ami_vector3d a_offset;
	/**< acceleration sensor deviation  */
	signed short a_deviation;
#endif
};

/** G2-Sensor measurement value (voltage ADC value ) */
struct ami_sensor_rawvalue {
	/**< geomagnetic field sensor measurement X-axis value
	(mounted position/direction reference) */
	unsigned short mx;
	/**< geomagnetic field sensor measurement Y-axis value
	(mounted position/direction reference) */
	unsigned short my;
	/**< geomagnetic field sensor measurement Z-axis value
	(mounted position/direction reference) */
	unsigned short mz;
#ifdef AMI_6AXIS
	/**< acceleration sensor measurement X-axis value
	(mounted position/direction reference) */
	unsigned short ax;
	/**< acceleration sensor measurement Y-axis value
	(mounted position/direction reference) */
	unsigned short ay;
	/**< acceleration sensor measurement Z-axis value
	(mounted position/direction reference) */
	unsigned short az;
#endif
	/**< temperature sensor measurement value  */
	unsigned short temperature;
};

/** Window function Parameter information  */
struct ami_win_parameter {
	/**< current fine value  */
	struct ami_vector3d m_fine;
	/**< change per 1coarse */
	struct ami_vector3d m_fine_output;
	/**< fine value at zero gauss */
	struct ami_vector3d m_0Gauss_fine;
#ifdef AMI304
	/**< current b0 value  */
	struct ami_vector3d m_b0;
	/**< current coarse value  */
	struct ami_vector3d m_coar;
	/**< change per 1fine */
	struct ami_vector3d m_coar_output;
	/**< coarse value at zero gauss */
	struct ami_vector3d m_0Gauss_coar;
	/**< delay value  */
	struct ami_vector3d m_delay;
#endif
};

/** AMI chip information ex) 1)model 2)s/n 3)ver 4)more info in the chip */
struct ami_chipinfo {
	unsigned short info;	/* INFO 0x0d/0x0e reg.  */
	unsigned short ver;	/* VER  0xe8/0xe9 reg.  */
	unsigned short sn;	/* SN   0xea/0xeb reg.  */
	unsigned char wia;	/* WIA  0x0f      reg.  */
};

/** AMI Driver Information  */
struct ami_driverinfo {
	unsigned char remarks[40];	/* Some Information   */
	unsigned char datetime[30];	/* compiled date&time */
	unsigned char ver_major;	/* major version */
	unsigned char ver_middle;	/* middle.. */
	unsigned char ver_minor;	/* minor .. */
};

#endif
