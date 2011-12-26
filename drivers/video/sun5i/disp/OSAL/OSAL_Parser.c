#include "OSAL_Parser.h"

int OSAL_Script_FetchParser_Data(char *main_name, char *sub_name, int value[], int count)
{
	return script_parser_fetch(main_name, sub_name, value, count);
}

int OSAL_sw_get_ic_ver(void)
{
    return 0xB;
}
