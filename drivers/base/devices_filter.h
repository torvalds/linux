#ifndef __DEVICES_FILTER_H
#define __DEVICES_FILTER_H
//lt 2010-12-03...

#define USB_ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

typedef  struct {
	int  vid;
	int  pid;
	const char *manufacturer;
	const char *product;
	void *data;
}usb_parameter;

static usb_parameter not_skip_device_list[] = {
	{0x12D1, 0x1001, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", "HUAWEI Mobile",NULL},
//	{0x12D1, 0x1411, "HUAWEI Technology", "HUAWEI Mobile",NULL},
};

static usb_parameter skip_device_list[] = {
	{0x12D1, 0x1DA1, NULL, NULL, NULL},
	{0x12D1, 0x1D09, NULL, NULL, NULL},
	{0x12D1, 0x1520, NULL, NULL, NULL},
	{0x12D1, 0x14AC, NULL, NULL, NULL},
	{0x12D1, 0x1465, NULL, NULL, NULL},
	{0x12D1, 0x1446, NULL, NULL, NULL},
	{0x12D1, 0x1414, NULL, NULL, NULL},
	{0x12D1, 0x140C, NULL, NULL, NULL},
	{0x12D1, 0x1003, NULL, NULL, NULL},
	{0x12D1, 0x1001, NULL, NULL, NULL},
        {0x12D1, 0x1505, NULL, NULL, NULL},
        {0x12D1, 0x14FE, NULL, NULL, NULL}, 
        {0x12D1, 0x1506, NULL, NULL, NULL},
	{0x19D2, 0xFFF5, NULL, NULL, NULL},
	{0x19D2, 0xFFF1, NULL, NULL, NULL},
	{0x19D2, 0x2000, NULL, NULL, NULL},
	{0x19D2, 0x0120, NULL, NULL, NULL},
	{0x19D2, 0x0094, NULL, NULL, NULL},
	{0x19D2, 0x0079, NULL, NULL, NULL},
	{0x19D2, 0x0073, NULL, NULL, NULL},
	{0x19D2, 0x0033, NULL, NULL, NULL},
	{0x19D2, 0x0031, NULL, NULL, NULL},
	{0x19D2, 0x0026, NULL, NULL, NULL},
	{0x19D2, 0x0017, NULL, NULL, NULL},
	{0x19D2, 0x0015, NULL, NULL, NULL},
        {0x19D2, 0x0154, NULL, NULL, NULL},        
        {0x19D2, 0x2003, NULL, NULL, NULL},
		{0x19D2, 0x0151, NULL, NULL, NULL},         
	{0x04CC, 0x225A, NULL, NULL, NULL},
	{0x04CC, 0x2259, NULL, NULL, NULL},

	{0x1410, 0x5010, NULL, NULL, NULL},
	{0x1410, 0x4400, NULL, NULL, NULL},
	
	{0x1D09, 0xAEF4, NULL, NULL, NULL},

	{0x1BBB, 0xF000, NULL, NULL, NULL},
	{0x1BBB, 0x0017, NULL, NULL, NULL},
	
// 山寨
//	{0x05C6, -1, NULL, NULL, NULL},
	{0x05C6, 0x1000, NULL, NULL, NULL},
        {0x05C6, 0xF000, NULL, NULL, NULL},
        {0x05C6, 0x0016, NULL, NULL, NULL},
	{0x1AB7, 0x5700, NULL, NULL, NULL},
	{0x20B9, 0x1682, NULL, NULL, NULL},

	{0x028A, 0x1006, NULL, NULL, NULL},
	
	{0x1C9E, 0x6061, NULL, NULL, NULL},
	{0x1C9E, 0x6000, NULL, NULL, NULL},
	{0x1C9E, 0x1001, NULL, NULL, NULL},
	
	{0x1C9E, 0xF000, NULL, NULL, NULL},    
    {0x1C9E, 0x9605, NULL, NULL, NULL},	
	{0x21F5, 0x2008, NULL, NULL, NULL},
	{0x21F5, 0x1000, NULL, NULL, NULL},
	
	{0x6000, 0x1000, NULL, NULL, NULL},

	{0x0685, 0x7000, NULL, NULL, NULL},

	{0x20A6, 0xF00E, NULL, NULL, NULL},
	{0x20A6, 0x1105, NULL, NULL, NULL},
	{0x1614, 0x0800, NULL, NULL, NULL},
	//begain add by xxh
        {0x1A8D, 0x2000, NULL, NULL, NULL},
        {0x1C9E, 0x9913, NULL, NULL, NULL},
        {0x1C9E, 0x9915, NULL, NULL, NULL},
        {0x230D, 0x000C, NULL, NULL, NULL},
        {0x230D, 0x0003, NULL, NULL, NULL},
		{0x230D, 0x0001, NULL, NULL, NULL},
        {0x257A, 0x2601, NULL, NULL, NULL},
        {0x1E89, 0x1E16, NULL, NULL, NULL},
        {0x1C9E, 0x9800, NULL, NULL, NULL},
		{0x1dbc, 0x0669, NULL, NULL, NULL},  
		{0x2020, 0x0002, NULL, NULL, NULL},    
		{0x2020, 0x4000, NULL, NULL, NULL},  
	    {0x1C9E, 0x9916, NULL, NULL, NULL},     
		{0x1C9E, 0x9918, NULL, NULL, NULL},     
	    {0x2001, 0xA80B, NULL, NULL, NULL},      
	    {0x2001, 0x7D00, NULL, NULL, NULL},   
		{0x0421, 0x0610, NULL, NULL, NULL},    
        {0x0af0, 0xd157, NULL, NULL, NULL},      
	    {0x19d2, 0x1216, NULL, NULL, NULL},         
		{0x19d2, 0x1218, NULL, NULL, NULL},
		{0x05c6, 0x9000, NULL, NULL, NULL},
		{0x19d2, 0x0016, NULL, NULL, NULL},//MF110
		{0x1C9E, 0x9914, NULL, NULL, NULL},//SMART BRO WM66E
		{0x2077, 0xF000, NULL, NULL, NULL},
        {0x2077, 0xA000, NULL, NULL, NULL},//beyond
	    {0x12d1, 0x140b, NULL, NULL, NULL},//EC122 India
	    {0x19D2, 0x1266, NULL, NULL, NULL},//MF190U
	    {0x19D2, 0x1514, NULL, NULL, NULL},
        {0x19D2, 0x1515, NULL, NULL, NULL},//MF195
		{0x1bbb, 0xf017, NULL, NULL, NULL},
		{0x19d2, 0x0117, NULL, NULL, NULL},
		{0x1C9E, 0xF001, NULL, NULL, NULL},
		{0x1C9E, 0x9801, NULL, NULL, NULL},
		//end xxh
};

/* 1 - match
  * 0 - no match
  */
static int __must_check inline match_for_each_entry(usb_parameter* pusbp1, usb_parameter* pusbp2)
{
	if( pusbp1->vid>=0 && pusbp1->vid!=pusbp2->vid)
	    return 0;

	if( pusbp1->pid>=0 && pusbp1->pid!=pusbp2->pid)
	    return 0;

    if( pusbp1->manufacturer )
    {
        if( pusbp2->manufacturer==NULL )
            return 0;
        else if( strcmp(pusbp1->manufacturer, pusbp2->manufacturer) )
            return 0;
    }

    if( pusbp1->product )
    {
        if( pusbp2->product==NULL )
            return 0;
        else if( strcmp(pusbp1->product, pusbp2->product) )
            return 0;
    }

    return 1;
}

/*
    0 - don't skip
    1 - skip the device
 */
static int is_skip_device(usb_parameter *pusbp)
{
	int i = 0;
/*
    在 not_skip_device_list 中寻找匹配，找到则该 device 正常流程
    在 skip_device_list 中寻找匹配，找到则该 device 跳过attach
    对于不在 skip_device_list，not_skip_device_list，则正常流程
 */
	for(i = 0; i < USB_ARRAY_SIZE(not_skip_device_list); i++)
	{
		if(match_for_each_entry(not_skip_device_list+i, pusbp)){
		    return 0;
		}
	}
	
	for(i = 0; i < USB_ARRAY_SIZE(skip_device_list); i++)
	{
		if(match_for_each_entry(skip_device_list+i, pusbp)){
		    return 1;
		}
	}

	return 0;
}

#endif //__DEVICES_FILTER_H
