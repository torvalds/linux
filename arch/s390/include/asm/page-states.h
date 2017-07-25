/*
 *    Copyright IBM Corp. 2017
 *    Author(s): Claudio Imbrenda <imbrenda@linux.vnet.ibm.com>
 */

#ifndef PAGE_STATES_H
#define PAGE_STATES_H

#define ESSA_GET_STATE			0
#define ESSA_SET_STABLE			1
#define ESSA_SET_UNUSED			2
#define ESSA_SET_VOLATILE		3
#define ESSA_SET_POT_VOLATILE		4
#define ESSA_SET_STABLE_RESIDENT	5
#define ESSA_SET_STABLE_IF_RESIDENT	6
#define ESSA_SET_STABLE_NODAT		7

#define ESSA_MAX	ESSA_SET_STABLE_IF_RESIDENT

#endif
