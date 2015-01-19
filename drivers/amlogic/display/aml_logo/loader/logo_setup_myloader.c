/******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *		this file is an examble for how to setup parameter by other loader 
 *
 *  Author: Amlogic Software
 *  Created: 2010/4/1   19:46
 *
 *******************************************************************/

 /******************logo entry point ***************/
static int logo_para_setup(void)
{
	//todo 
	//	1  load parameter from nand or flash 
	//    2  setup logo object 
	return 0;
}
subsys_initcall_sync(logo_para_setup) ;