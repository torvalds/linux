/*
 * drivers/input/touchscreen/calibration_ts.c - calibration for rk2818 spi xpt2046 device and console
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/string.h>

#include "calibration_ts.h"
#include "largenum_ts.h"

#define MAX_POINT_ERROR 6

typedef struct {
    PLARGENUM   pa11, pa12, pa13;
    PLARGENUM   pa21, pa22, pa23;
    PLARGENUM   pa31, pa32, pa33;
}   MATRIX33, *PMATRIX33;

typedef struct {
    int   a1;
    int   b1;
    int   c1;
    int   a2;
    int   b2;
    int   c2;
    int   delta;
}   
CALIBRATION_PARAMETER, *PCALIBRATION_PARAMETER;

static unsigned char            v_Calibrated = 0;
static CALIBRATION_PARAMETER    v_CalcParam ={
	.a1		=18670 ,
	.b1		=98,
	.c1		= -2230109,
	.a2   	= 291,
	.b2  		= 12758,
	.c2  		= -5118934,
	.delta	= 91931,
};
static CALIBRATION_PARAMETER v_CalcParam_bak = {
	.a1=17704 ,
	.b1=-20,
	.c1= -1460283,
	.a2   = 382,
	.b2  = 12685,
	.c2  = -5595261,
	.delta	 = 88403,
};
unsigned char
ErrorAnalysis(
    int   cCalibrationPoints,     //@PARM The number of calibration points
    int   *pScreenXBuffer,        //@PARM List of screen X coords displayed
    int   *pScreenYBuffer,        //@PARM List of screen Y coords displayed
    int   *pUncalXBuffer,         //@PARM List of X coords collected
    int   *pUncalYBuffer          //@PARM List of Y coords collected
    );

void
ComputeMatrix33(
    PLARGENUM   pResult,
    PMATRIX33   pMatrix
    );

unsigned char
TouchPanelSetCalibration(
    int   cCalibrationPoints,     //@PARM The number of calibration points
    int   *pScreenXBuffer,        //@PARM List of screen X coords displayed
    int   *pScreenYBuffer,        //@PARM List of screen Y coords displayed
    int   *pUncalXBuffer,         //@PARM List of X coords collected
    int   *pUncalYBuffer          //@PARM List of Y coords collected
    )
{
    LARGENUM    a11;
    LARGENUM    a21, a22;
    LARGENUM    a31, a32, a33;
    LARGENUM    b11, b12, b13;
    LARGENUM    b21, b22, b23;
    LARGENUM    lnScreenX;
    LARGENUM    lnScreenY;
    LARGENUM    lnTouchX;
    LARGENUM    lnTouchY;
    LARGENUM    lnTemp;
    LARGENUM    delta;
    LARGENUM    a1, b1, c1;
    LARGENUM    a2, b2, c2;
    MATRIX33    Matrix;
    int       cShift;
    int       minShift;
    int         i;


    //DEBUGMSG(1,(__TEXT("calibrating %d point set\r\n"), cCalibrationPoints));

     //
     // If the calibration data is being cleared, set the flag so
     // that the conversion operation is a noop.
     //

    if ( cCalibrationPoints == 0 )
    {
        v_Calibrated = 0;
        return 1;
    }

    //
    // Compute these large numbers
    //
    LargeNumSet(&a11, 0);
    LargeNumSet(&a21, 0);
    LargeNumSet(&a31, 0);
    LargeNumSet(&a22, 0);
    LargeNumSet(&a32, 0);
    LargeNumSet(&a33, cCalibrationPoints);
    LargeNumSet(&b11, 0);
    LargeNumSet(&b12, 0);
    LargeNumSet(&b13, 0);
    LargeNumSet(&b21, 0);
    LargeNumSet(&b22, 0);
    LargeNumSet(&b23, 0);
    for(i=0; i<cCalibrationPoints; i++){
        LargeNumSet(&lnTouchX, pUncalXBuffer[i]);
        LargeNumSet(&lnTouchY, pUncalYBuffer[i]);
        LargeNumSet(&lnScreenX, pScreenXBuffer[i]);
        LargeNumSet(&lnScreenY, pScreenYBuffer[i]);
        LargeNumMult(&lnTouchX, &lnTouchX, &lnTemp);
        LargeNumAdd(&a11, &lnTemp, &a11);
        LargeNumMult(&lnTouchX, &lnTouchY, &lnTemp);
        LargeNumAdd(&a21, &lnTemp, &a21);
        LargeNumAdd(&a31, &lnTouchX, &a31);
        LargeNumMult(&lnTouchY, &lnTouchY, &lnTemp);
        LargeNumAdd(&a22, &lnTemp, &a22);
        LargeNumAdd(&a32, &lnTouchY, &a32);
        LargeNumMult(&lnTouchX, &lnScreenX, &lnTemp);
        LargeNumAdd(&b11, &lnTemp, &b11);
        LargeNumMult(&lnTouchY, &lnScreenX, &lnTemp);
        LargeNumAdd(&b12, &lnTemp, &b12);
        LargeNumAdd(&b13, &lnScreenX, &b13);
        LargeNumMult(&lnTouchX, &lnScreenY, &lnTemp);
        LargeNumAdd(&b21, &lnTemp, &b21);
        LargeNumMult(&lnTouchY, &lnScreenY, &lnTemp);
        LargeNumAdd(&b22, &lnTemp, &b22);
        LargeNumAdd(&b23, &lnScreenY, &b23);
    }

    Matrix.pa11 = &a11;
    Matrix.pa21 = &a21;
    Matrix.pa31 = &a31;
    Matrix.pa12 = &a21;
    Matrix.pa22 = &a22;
    Matrix.pa32 = &a32;
    Matrix.pa13 = &a31;
    Matrix.pa23 = &a32;
    Matrix.pa33 = &a33;
    ComputeMatrix33(&delta, &Matrix);

    Matrix.pa11 = &b11;
    Matrix.pa21 = &b12;
    Matrix.pa31 = &b13;
    ComputeMatrix33(&a1, &Matrix);

    Matrix.pa11 = &a11;
    Matrix.pa21 = &a21;
    Matrix.pa31 = &a31;
    Matrix.pa12 = &b11;
    Matrix.pa22 = &b12;
    Matrix.pa32 = &b13;
    ComputeMatrix33(&b1, &Matrix);

    Matrix.pa12 = &a21;
    Matrix.pa22 = &a22;
    Matrix.pa32 = &a32;
    Matrix.pa13 = &b11;
    Matrix.pa23 = &b12;
    Matrix.pa33 = &b13;
    ComputeMatrix33(&c1, &Matrix);

    Matrix.pa13 = &a31;
    Matrix.pa23 = &a32;
    Matrix.pa33 = &a33;
    Matrix.pa11 = &b21;
    Matrix.pa21 = &b22;
    Matrix.pa31 = &b23;
    ComputeMatrix33(&a2, &Matrix);

    Matrix.pa11 = &a11;
    Matrix.pa21 = &a21;
    Matrix.pa31 = &a31;
    Matrix.pa12 = &b21;
    Matrix.pa22 = &b22;
    Matrix.pa32 = &b23;
    ComputeMatrix33(&b2, &Matrix);

    Matrix.pa12 = &a21;
    Matrix.pa22 = &a22;
    Matrix.pa32 = &a32;
    Matrix.pa13 = &b21;
    Matrix.pa23 = &b22;
    Matrix.pa33 = &b23;
    ComputeMatrix33(&c2, &Matrix);

#if 1
    {
        LARGENUM    halfDelta;
        //
        // Take care of possible truncation error in later mapping operations
        //
        if(IsLargeNumNegative(&delta)){
            LargeNumDivInt32(&delta, -2, &halfDelta);
        } else {
            LargeNumDivInt32(&delta, 2, &halfDelta);
        }
        LargeNumAdd(&c1, &halfDelta, &c1);
        LargeNumAdd(&c2, &halfDelta, &c2);
    }
#endif

    //
    // All the numbers are determined now.
    // Let's scale them back to 32 bit world
    //
    minShift = 0;
    cShift = LargeNumBits(&a1) - MAX_COEFF_PRECISION;
    if(cShift > minShift){
        minShift = cShift;
    }
    cShift = LargeNumBits(&b1) - MAX_COEFF_PRECISION;
    if(cShift > minShift){
        minShift = cShift;
    }
    cShift = LargeNumBits(&a2) - MAX_COEFF_PRECISION;
    if(cShift > minShift){
        minShift = cShift;
    }
    cShift = LargeNumBits(&b2) - MAX_COEFF_PRECISION;
    if(cShift > minShift){
        minShift = cShift;
    }
    cShift = LargeNumBits(&c1) - MAX_TERM_PRECISION;
    if(cShift > minShift){
        minShift = cShift;
    }
    cShift = LargeNumBits(&c2) - MAX_TERM_PRECISION;
    if(cShift > minShift){
        minShift = cShift;
    }
    cShift = LargeNumBits(&delta) - 31;
    if(cShift > minShift){
        minShift = cShift;
    }

    //
    // Now, shift count is determined, shift all the numbers
    //  right to obtain the 32-bit signed values
    //
    if(minShift){
        LargeNumRAShift(&a1, minShift);
        LargeNumRAShift(&a2, minShift);
        LargeNumRAShift(&b1, minShift);
        LargeNumRAShift(&b2, minShift);
        LargeNumRAShift(&c1, minShift);
        LargeNumRAShift(&c2, minShift);
        LargeNumRAShift(&delta, minShift);
    }
    v_CalcParam.a1      = a1.u.s32.u[0];
    v_CalcParam.b1      = b1.u.s32.u[0];
    v_CalcParam.c1      = c1.u.s32.u[0];
    v_CalcParam.a2      = a2.u.s32.u[0];
    v_CalcParam.b2      = b2.u.s32.u[0];
    v_CalcParam.c2      = c2.u.s32.u[0];
    v_CalcParam.delta   = delta.u.s32.u[0];

     // Don't allow delta to be zero, since it gets used as a divisor
    if( ! v_CalcParam.delta )
    {
        //RETAILMSG(1,(__TEXT("TouchPanelSetCalibration: delta of 0 invalid\r\n")));
        //RETAILMSG(1,(__TEXT("\tCalibration failed.\r\n")));
        v_CalcParam.delta = 1;  // any non-zero value to prevents DivByZero traps later
        v_Calibrated = 0;
    }
    else
        v_Calibrated = 1;

    return ErrorAnalysis(
                    cCalibrationPoints,
                    pScreenXBuffer,
                    pScreenYBuffer,
                    pUncalXBuffer,
                    pUncalYBuffer
                );
}

void
ComputeMatrix33(
    PLARGENUM   pResult,
    PMATRIX33   pMatrix
    )
{
    LARGENUM    lnTemp;

    LargeNumMult(pMatrix->pa11, pMatrix->pa22, &lnTemp);
    LargeNumMult(pMatrix->pa33, &lnTemp, pResult);
    LargeNumMult(pMatrix->pa21, pMatrix->pa32, &lnTemp);
    LargeNumMult(pMatrix->pa13, &lnTemp, &lnTemp);
    LargeNumAdd(pResult, &lnTemp, pResult);
    LargeNumMult(pMatrix->pa12, pMatrix->pa23, &lnTemp);
    LargeNumMult(pMatrix->pa31, &lnTemp, &lnTemp);
    LargeNumAdd(pResult, &lnTemp, pResult);
    LargeNumMult(pMatrix->pa13, pMatrix->pa22, &lnTemp);
    LargeNumMult(pMatrix->pa31, &lnTemp, &lnTemp);
    LargeNumSub(pResult, &lnTemp, pResult);
    LargeNumMult(pMatrix->pa12, pMatrix->pa21, &lnTemp);
    LargeNumMult(pMatrix->pa33, &lnTemp, &lnTemp);
    LargeNumSub(pResult, &lnTemp, pResult);
    LargeNumMult(pMatrix->pa23, pMatrix->pa32, &lnTemp);
    LargeNumMult(pMatrix->pa11, &lnTemp, &lnTemp);
    LargeNumSub(pResult, &lnTemp, pResult);
}

void
TouchPanelCalibrateAPoint(
    int   UncalX,     //@PARM The uncalibrated X coordinate
    int   UncalY,     //@PARM The uncalibrated Y coordinate
    int   *pCalX,     //@PARM The calibrated X coordinate
    int   *pCalY      //@PARM The calibrated Y coordinate
    )
{
    int   x, y;

    if ( v_Calibrated )
    {

     //
     // Note the *4 in the expression below.  This is a workaround
     // on behalf of gwe.  It provides a form of
     // sub-pixel accuracy desirable for inking
     //
    	x = (v_CalcParam.a1 * UncalX + v_CalcParam.b1 * UncalY +
         	v_CalcParam.c1) * 4 / v_CalcParam.delta;
    	y = (v_CalcParam.a2 * UncalX + v_CalcParam.b2 * UncalY +
         	v_CalcParam.c2) * 4 / v_CalcParam.delta;
     }
     else{
	 	x = (v_CalcParam_bak.a1 * UncalX + v_CalcParam_bak.b1 * UncalY +
         		v_CalcParam_bak.c1) * 4 / v_CalcParam_bak.delta;
    		y = (v_CalcParam_bak.a2 * UncalX + v_CalcParam_bak.b2 * UncalY +
         		v_CalcParam_bak.c2) * 4 / v_CalcParam_bak.delta;
     }
    if ( x < 0 ){
        x = 0;
    }

    if  (y < 0 ){
        y = 0;
    }

    *pCalX = x;
    *pCalY = y;
}

unsigned char
ErrorAnalysis(
    int   cCalibrationPoints,     //@PARM The number of calibration points
    int   *pScreenXBuffer,        //@PARM List of screen X coords displayed
    int   *pScreenYBuffer,        //@PARM List of screen Y coords displayed
    int   *pUncalXBuffer,         //@PARM List of X coords collected
    int   *pUncalYBuffer          //@PARM List of Y coords collected
    )
{
    int     i;
    unsigned int  maxErr, err;
    int   x,y;
    int   dx,dy;
    unsigned int  errThreshold = MAX_POINT_ERROR;  // Can be overridden by registry entry
#if 0
    unsigned int  status, ValType, ValLen;

    //HKEY    regKey;


    // See if there is a Maximum Calibration Error specified in the registry
    //status = RegOpenKeyEx(
    //                     HKEY_LOCAL_MACHINE,
     //                    __TEXT("HARDWARE\\DEVICEMAP\\TOUCH"),
    //                     0,
    //                     0,
    //                     &regKey);
    if ( status == ERROR_SUCCESS ) {
        ValLen = sizeof(errThreshold);
        status = RegQueryValueEx(
                                regKey,
                                __TEXT("MaxCalError"),
                                NULL,
                                &ValType,
                                (PUCHAR)&errThreshold,
                                &ValLen);
        // We don't care what happened.  Either we have a new value or we have the default value.
        RegCloseKey(regKey);        
    }

    RETAILMSG(1,(__TEXT("Maximum Allowed Error %d:\r\n"),
                errThreshold));
    DEBUGMSG(1,(__TEXT("Calibration Results:\r\n")));
#endif

    maxErr = 0;
    //DEBUGMSG(1,(__TEXT("   Screen    =>    Mapped\r\n")));
    for(i=0; i<cCalibrationPoints; i++){
        TouchPanelCalibrateAPoint(  pUncalXBuffer[i],
                                    pUncalYBuffer[i],
                                    &x,
                                    &y
                                    );
        x /= 4;
        y /= 4;
        printk("(%4d, %4d) => (%4d, %4d)\n",
        //DEBUGMSG(1,(__TEXT("(%4d, %4d) => (%4d, %4d)\r\n"),
                pScreenXBuffer[i],
                pScreenYBuffer[i],
                x,
                y
                );
	 
        dx = x - pScreenXBuffer[i];
        dy = y - pScreenYBuffer[i];
        err = dx * dx + dy * dy;
        if(err > maxErr){
            maxErr = err;
        }
    }
    //DEBUGMSG(1,(__TEXT("Maximum error (square of Euclidean distance in screen units) = %u\r\n"),
     //       maxErr
    //        ));

    if (maxErr < (errThreshold * errThreshold))
    {
    		 printk(" v_CalcParam.a1=%d \n"	 
    			"v_CalcParam.b1=%d\n"	
    			"v_CalcParam.c1= %d\n"	 
  			 " v_CalcParam.a2   = %d\n" 
   			" v_CalcParam.b2  = %d\n"	
   			" v_CalcParam.c2  = %d\n"
    			"v_CalcParam.delta   = %d\n",    
   			 v_CalcParam.a1 , \
    			v_CalcParam.b1 , 	\
    			v_CalcParam.c1 ,	\
    			v_CalcParam.a2 ,	\
    			v_CalcParam.b2,	\
    			v_CalcParam.c2 ,	\
    			v_CalcParam.delta);
        return 1;
    }
    else
    {
       memset(&v_CalcParam, 0, sizeof(v_CalcParam));
       v_Calibrated = 0;
       v_CalcParam.a1 =  v_CalcParam_bak.a1;
    	v_CalcParam.b1 = v_CalcParam_bak.b1 ;
    	v_CalcParam.c1=  v_CalcParam_bak.c1;
    	v_CalcParam.a2 = v_CalcParam_bak.a2;
    	v_CalcParam.b2  = v_CalcParam_bak.b2;
    	v_CalcParam.c2 =  v_CalcParam_bak.c2;
    	v_CalcParam.delta=  v_CalcParam_bak.delta;
        return 0;
    }
}

#define FILTER_BUF_LEN 8
typedef struct 
{
	unsigned short x;
	unsigned short y;
}P;
static P sTouchFilterBuff[FILTER_BUF_LEN];
static int sBuffIndex = 0;
static P sReportFilter = {0,0};
void ClearBuff(void)
{
	memset(sTouchFilterBuff,0,FILTER_BUF_LEN*sizeof(P));
	sReportFilter.x = 0;
	sReportFilter.y = 0;
	sBuffIndex = 0;
}
void addToBuff(int* x,int* y)
{	
	int index;
	index=sBuffIndex++%FILTER_BUF_LEN;
	sTouchFilterBuff[index].x = *x;
	sTouchFilterBuff[index].y = *y;
}
#define TS_ERR_TDOWN -1
#define TS_ERR_LOWBUF -2
//#define TS_MINX	138
//#define TS_MINY	375
//#define TS_MAXX	3935
//#define TS_MAXY	3920
//#define TS_xISVALID(x) (x>=TS_MINX&&x<=TS_MAXX)
#define TS_isINVALID(X,Y) (X==4095||Y==4095||X==0||Y==0)
#define ABS(x) ((x)>0?(x):-(x))
static P spoint;
int TouchFilter(unsigned short* x,unsigned short* y,bool isdown)
{
#if 1
	int ret = 0;
	if(isdown==0&&sBuffIndex==0)
		{
		spoint.x = *x;
		spoint.y = *y;
		ClearBuff();
		ret=TS_ERR_TDOWN;
		}
	if(!TS_isINVALID(*x,*y))
		addToBuff(x,y);
	if(sBuffIndex<FILTER_BUF_LEN)
		ret=TS_ERR_LOWBUF;
	if(ret==0)
		{
		P *p = sTouchFilterBuff;
		P *p1 = p+1;
		int index =0;
		while(1)
			{
			if(ABS(p->x-p1->x)<60||ABS(p->y-p1->y)<60)
				{
				*x=spoint.x;
				*y=spoint.y;
				//printk("p(%d,%d)	p1(%d,%d)\n",p->x,p->y,p1->x,p1->y);
				//ret=-3;
				break;
				}
			p++;
			p1++;
			if(++index>=FILTER_BUF_LEN-1)break;
			}
		spoint.x=*x;
		spoint.y=*y;
		}
	
#else	
	int ret = 0;
	if(isdown==0&&sBuffIndex==0)
		{
		ClearBuff();
		ret=TS_ERR_TDOWN;
		}
	if(!TS_isINVALID(x,y))
		addToBuff(x,y);
	if(sBuffIndex<FILTER_BUF_LEN)
		ret=TS_ERR_LOWBUF;
	if(ret==0)
		{
		P adp;
		int index =0;
		while(1)
			{
			adp.x+=sTouchFilterBuff[index].x;
			adp.y+=sTouchFilterBuff[index].y;
			if(++index>=FILTER_BUF_LEN)break;
			}
		*x = adp.x/FILTER_BUF_LEN;
		*y = adp.y/FILTER_BUF_LEN;
		}
#endif
	return ret;
	
}

#define SLAP_X 2
#define SLAP_Y 0
void TouchReportFilter(unsigned short* x,unsigned short* y)
{
	if((sReportFilter.x==0&&sReportFilter.y==0)||
		(ABS(sReportFilter.x - *x)>SLAP_X&&ABS(sReportFilter.y - *y)>SLAP_Y))
		{
		sReportFilter.x = *x;
		sReportFilter.y = *y;
		}
	*x = sReportFilter.x;
	*y = sReportFilter.y;
}
#if 0
int main(void)
{
    unsigned char ret;
    int cali_num = 4;
    int screen_x[4], screen_y[4];
    int uncali_x[4], uncali_y[4];
    int tst_uncali_x, tst_uncali_y, tst_cali_x, tst_cali_y;
    
    screen_x[0] = 15; screen_y[0] = 15;
    screen_x[1] = 15; screen_y[1] = 465;
    screen_x[2] = 785; screen_y[2] = 15;
    screen_x[3] = 785; screen_y[3] = 465;
    
    uncali_x[0] = 173; uncali_y[0] = 417;
    uncali_x[1] = 148; uncali_y[1] = 3867;
    uncali_x[2] = 3903; uncali_y[2] = 365;
    uncali_x[3] = 3924; uncali_y[3] = 3863;
    
    ret = TouchPanelSetCalibration(4, screen_x, 
            screen_y, uncali_x, uncali_y);
    if (ret == 1)
        printf("TouchPanelSetCalibration OK.\n");
    else
        printf("TouchPanelSetCalibration FAIL.\n");

    tst_uncali_x = 2033;
    tst_uncali_y = 2132;
    
    TouchPanelCalibrateAPoint(tst_uncali_x, tst_uncali_y,
                              &tst_cali_x, &tst_cali_y);
    
    printf("(%d, %d) >> (%d, %d)\n", tst_uncali_x, tst_uncali_y,
                                     tst_cali_x/4, tst_cali_y/4);
    
    tst_uncali_x = 170;
    tst_uncali_y = 418;
    
    TouchPanelCalibrateAPoint(tst_uncali_x, tst_uncali_y,
                              &tst_cali_x, &tst_cali_y);
    
    printf("(%d, %d) >> (%d, %d)\n", tst_uncali_x, tst_uncali_y,
                                     tst_cali_x/4, tst_cali_y/4);

    tst_uncali_x = 500;
    tst_uncali_y = 707;
    
    TouchPanelCalibrateAPoint(tst_uncali_x, tst_uncali_y,
                              &tst_cali_x, &tst_cali_y);
    
    printf("(%d, %d) >> (%d, %d)\n", tst_uncali_x, tst_uncali_y,
                                     tst_cali_x/4, tst_cali_y/4);

    tst_uncali_x = 3636;
    tst_uncali_y = 2150;
    
    TouchPanelCalibrateAPoint(tst_uncali_x, tst_uncali_y,
                              &tst_cali_x, &tst_cali_y);
    
    printf("(%d, %d) >> (%d, %d)\n", tst_uncali_x, tst_uncali_y,
                                     tst_cali_x/4, tst_cali_y/4);
                                                  
    return 0;    
}
#endif

