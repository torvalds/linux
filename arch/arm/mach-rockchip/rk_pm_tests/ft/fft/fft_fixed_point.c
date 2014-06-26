#include "fft_fixed_point.h"
#if 0
#include <stdlib.h>
#include <string.h>
#else

#include <linux/string.h>
#include <linux/resume-trace.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/freezer.h>
#include <linux/vmalloc.h>
#define free(ptr) kfree((ptr))
//typedef unsigned char uchar;

#define malloc(size) kmalloc((size), GFP_ATOMIC)

#define printf(fmt, arg...) \
		printk(KERN_EMERG fmt, ##arg)

#endif


/***********************************************查找表*************************************************************/

//全局变量,2的整数次幂表
int Pow2_table[20] = {1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768,65536,131072,262144,524288};

//对上表进行定点化后的sin函数查找表，即将上表中每个数乘上 2^8 = 256
int sin_table_INT_128[]={
	 0,  13,  25,  38,  50,  62,  74,  86,
	98, 109, 121, 132, 142, 152, 162, 172,
	181, 190, 198, 206, 213, 220, 226, 231,
	237, 241, 245, 248, 251, 253, 255, 256,
	256, 256, 255, 253, 251, 248, 245, 241,
	237, 231, 226, 220, 213, 206, 198, 190,
	181, 172, 162, 152, 142, 132, 121, 109,
	 98,  86,  74,  62,  50,  38,  25,  13
	
};
//FFT中点数为128的cos函数定点化后的查找表，
int cos_table_INT_128[]={
	256,  256,  255,  253,  251,  248,  245,  241,
	237,  231,  226,  220,  213,  206,  198,  190,
	181,  172,  162,  152,  142,  132,  121,  109,
	 98,   86,   74,   62,   50,   38,   25,   13,
	  0,  -13,  -25,  -38,  -50,  -62,  -74,  -86,
	 -98, -109, -121, -132, -142, -152, -162, -172,
	-181, -190, -198, -206, -213, -220, -226, -231,
	-237, -241, -245, -248, -251, -253, -255, -256
};

//FFT中输入序列重新排序后的序列 注意:只适用于点数为128
int reverse_matrix[]={
	0,  64,  32,  96,  16,  80,  48, 112,   8,  72,  40, 104,  24,  88,  56, 120,
	4,  68,  36, 100,  20,  84,  52, 116,  12,  76,  44, 108,  28,  92,  60, 124,
	2,  66,  34,  98,  18,  82,  50, 114,  10,  74,  42, 106,  26,  90,  58, 122,
	6,  70,  38, 102,  22,  86,  54, 118,  14,  78,  46, 110,  30,  94,  62, 126,
	1,  65,  33,  97,  17,  81,  49, 113,   9,  73,  41, 105,  25,  89,  57, 121,
	5,  69,  37, 101,  21,  85,  53, 117,  13,  77,  45, 109,  29,  93,  61, 125,
	3,  67,  35,  99,  19,  83,  51, 115,  11,  75,  43, 107,  27,  91,  59, 123,
	7,  71,  39, 103,  23,  87,  55, 119,  15,  79,  47, 111,  31,  95,  63, 127
};

/***********************************************查找表*************************************************************/



/***************************************************************************************
以下为实现二维矩阵的FFT变换的相关函数,设A为M行N列，则M,N必须是２的幂次数
Date:
    2012-05-07
****************************************************************************************/

/*************************************************************************
Function:
       reverse
Discription:
       序列的逆序排列
	   由于 x(n) 被反复地按奇、偶分组，所以流图输入端的排列不再是顺序的,
	   以下则是保证输出端是自然顺序时，输入端的变址处理
Input Arguments:
       len: 序列的长度
	   M:   长度对应的2的指数 如len = 8,M = 3
Output Arguments:
       b: 输入端变址处理后输出的倒位序
Author:
       Wu Lijuan
Note:
       N./A.
Date:
      $ID:  $ 
**************************************************************************/
void reverse(int len, int M,int *b)
{
	//注意b在外面必须初始化为0
    int i,j;

	//a的初始化,a用于存放变址后序列的二进制数
	char *a = (char *)malloc(sizeof(char)*M); // a用于存放M位二进制数
	memset(a,0,sizeof(char)*M);

    for(i=1; i<len; i++)     // i = 0 时，顺序一致
    {
        j = 0;
        while(a[j] != 0)
        {
            a[j] = 0;
            j++;
        }

        a[j] = 1;
        for(j=0; j<M; j++)
        {
            b[i] = b[i]+a[j] * Pow2_table[M-1-j];    //pow(2,M-1-j),将二进制a转换为10进制b
        }
    }
	free(a);
}
/*************************************************************************
Function:
       nexttopow2
Discription:
       y = nexttopow2(x);
	   则2^y为大于等于x的最小的2的正整数次幂的数字,
	   如x = 12，则y = 4(2^4 = 16)
Input Arguments:
       x 必须是正整数
Output Arguments:
       y: 2^y为大于等于x的最小的2的正整数次幂的数字
Author:
       Wu Lijuan
Note:
       N./A.
Date:
      2013.02.23
**************************************************************************/
int nexttopow2(int x)
{
	int y;
	int i = 0;
	while(x > Pow2_table[i])
	{
		i++;
	}
	y = i;
	return y;
}
/*************************************************************************
Function:
       fft_fixed_point
Discription:
       1D FFT变换 采用定点化计算，提高速度
Input Arguments:
       A: 用于FFT变换的序列,同时也是输出结果
       fft_nLen:　序列长度
	   fft_M:     长度对应的指数 eg, 8 = 2^3
Output Arguments:
      
Author:
       Wu Lijuan
Note:
       具体原理参考本文件夹下PPT:快速傅里叶变换（蝶形运算）P31
Date:
      2013.06.25 
**************************************************************************/
void fft_fixed_point(int fft_nLen, int fft_M, RK_complex_INT * A)
{
	int i;
	int L,dist,p,t;
	int temp1,temp2;
	RK_complex_INT *pr1,*pr2;
	//double temp = 2*PI/fft_nLen;
	
        int WN_Re,WN_Im;       //WN的实部和虚部
	int X1_Re,X1_Im;       //临时变量的实部和虚部
	int X2_Re,X2_Im;   
        int WN_X2_Re;
        int WN_X2_Im;

	for(L = 1; L <= fft_M; L++)         //完成M次蝶形的迭代过程
	{
		dist = Pow2_table[L-1];             //dist = pow(2,L-1); 蝶形运算两节点间的距离
		temp1 = Pow2_table[fft_M-L];        //temp1 = pow(2,fft_M-L);
		temp2 = Pow2_table[L];              //temp2 = pow(2,L); 
		for(t=0; t<dist; t++)                 //循环完成因子WN的变化
		{
			p = t * temp1;                    //p = t*pow(2,fft_M-L);  

			//WN_Re = int(cos(temp * p) * Pow2_table[Q_INPUT]);          //W的确定 (double)cos(2*PI*p/fft_nLen); 
  	//		WN_Im = int(-1*sin(temp * p) * Pow2_table[Q_INPUT]);     //(double)(-1*sin(2*PI*p/fft_nLen));
			WN_Re = cos_table_INT_128[p];          //Q15
			WN_Im = -sin_table_INT_128[p];
			
			//循环完成相同因子WN的蝶形运算
			for(i = t; i < fft_nLen; i = i + temp2)           //i=i+pow(2,L)   Note: i=i+pow(2,L) 
			{
				//X(k) = X1(k) + WN * X2(k); 前半部分X(k)计算公式
				//X(k) = X1(k) - WN * X2(k); 后半部分X(k)计算公式

				pr1 = A+i;
				pr2 = pr1+dist;

				X1_Re = pr1->real;        //X1_Re = A[i].real; 
				X1_Im = pr1->image;       //X1_Im = A[i].image;
				X2_Re = pr2->real;        //X2_Re = A[i+dist].real;
				X2_Im = pr2->image;       //X2_Im = A[i+dist].image;


				//计算WN * X2(k),是个复数
				WN_X2_Re = ((long long)WN_Re * X2_Re - (long long)WN_Im * X2_Im) >> Q_INPUT;
				WN_X2_Im = ((long long)WN_Im * X2_Re + (long long)WN_Re * X2_Im) >> Q_INPUT;
				
				//计算X(k) = X1(k) + WN * X2(k);
				pr1->real = X1_Re + WN_X2_Re;     //A[i].real = X1_Re + WN_X2_Re;
				pr1->image = X1_Im + WN_X2_Im;    //A[i].image = X1_Im + WN_X2_Im; 

				//计算X(k) = X1(k) - WN * X2(k);
				pr2->real = X1_Re - WN_X2_Re;     //A[i+dist].real = X1_Re - WN_X2_Re;
				pr2->image = X1_Im - WN_X2_Im;    //A[i+dist].image = X1_Im - WN_X2_Im;
			}
		}
	}
}

/*************************************************************************
Function:
       fft_2D_fixed_point
Discription:
       2维 FFT变换 
Input Arguments:
       mLen,nLen 矩阵的高宽
	   M,N;     长度对应的2的对数 M = log2(mlen), N = log2(nlen);
       A_In:　　要进行逆变换的矩阵,同时也是输出结果
	   flag:    0　正向FFT变换，　1   逆向FFT变换
Output Arguments:
       
Author:
       Wu Lijuan
Note:
       N./A.
Date:
      2013.06.25
**************************************************************************/
void fft_2D_fixed_point(int mLen,int nLen,int M,int N,RK_complex_INT *A_In,int flag)
{
	int i,j;
	int len = mLen * nLen;

	//int *b = NULL;
	int *b = reverse_matrix;      //直接查逆序表，提高效率
        RK_complex_INT *p;
        //A用于处理矩阵中每行的数据
        RK_complex_INT * A;

	if (flag == DXT_INVERSE)
	{
		p = A_In;

		for(i=0; i<len; i++)
		{
			//逆变换公式与正变换公式区别,其结果的虚部与真实结果的虚部差一个负号
			//但通常情况下，都是对ifft的模值处理，即Re*Re + Im*Im,故对结果没什么影响
			//只是应该知道有这个区别
			(p->image) *= -1;    //A_In[i].image = -A_In[i].image;
			p++;
		}
	}	

	
	A = (RK_complex_INT *)malloc(sizeof(RK_complex_INT)*nLen);


	//先对矩阵的每一行做FFT变换
	for(i=0; i<mLen; i++)
	{
		RK_complex_INT *pr1 = A;
		RK_complex_INT *pr2 = NULL;
		RK_complex_INT *pr3 = A_In + (i<<N);   //不同行的起始地址

		for(j=0; j<nLen; j++)
		{
			//A[j].real = A_In[i*nLen+b[j]].real;        //取出重新排序后的每行数据
			//A[j].image = A_In[i*nLen+b[j]].image;

			pr2 = pr3 + b[j];                       //取代上段代码，提高访问效率
			pr1->real = pr2->real;        
			pr1->image = pr2->image;
			pr1++;
		}


		fft_fixed_point(nLen,N,A);                                 //进行IFFT变换，变换后的结果仍存于A

		if (flag == DXT_FORWARD)
		{
			pr1 = A;             //重新置位指针
			pr2 = pr3;

			for(j=0; j<nLen; j++)
			{
				//A_In[i*nLen+j].real = A[j].real;           //将变换结果存入原矩阵中
				//A_In[i*nLen+j].image = A[j].image;

				pr2->real = pr1->real;
				pr2->image = pr1->image;
				pr1++;
				pr2++;
			}
		}
		else
		{
			pr1 = A;             //重新置位指针
			pr2 = pr3;

			for(j=0; j<nLen; j++)
			{
				//A_In[i*nLen+j].real = A[j].real/nLen;           //将变换结果存入原矩阵中
				//A_In[i*nLen+j].image = A[j].image/nLen;

				pr2->real = pr1->real >> N;
				pr2->image = pr1->image >>N;
				pr1++;
				pr2++;
			}	
		}

	}
	free(A);
	//free(b);

	//A用于处理矩阵中每列的数据
	A = (RK_complex_INT *)malloc(sizeof(RK_complex_INT)*mLen);


	//先对矩阵的每一列做FFT变换
	for(i=0; i<nLen; i++)
	{
		RK_complex_INT *pr1 = A;
		RK_complex_INT *pr2 = NULL;
		RK_complex_INT *pr3 = A_In + i;   //不同列的起始地址

		for(j=0; j<mLen; j++)
		{
			//A[j].real = A_In[b[j]*nLen+i].real;        //取出重新排序后的每行数据
			//A[j].image = A_In[b[j]*nLen+i].image;

			pr2 = pr3 + (b[j]<<N);
			pr1->real = pr2->real;
			pr1->image = pr2->image;
			pr1++;
		}

		fft_fixed_point(mLen,M,A);                                 //进行IFFT变换，变换后的结果仍存于A

		if (flag == DXT_FORWARD)
		{
			pr1 = A;             //重新置位指针
			pr2 = pr3;

			for(j=0; j<mLen; j++)
			{
				//A_In[j*nLen+i].real = A[j].real;           //将变换结果存入原矩阵中
				//A_In[j*nLen+i].image = A[j].image;

				pr2->real = pr1->real;
				pr2->image = pr1->image;
				pr1++;
				pr2 += nLen;
			}	
		}
		else
		{
			pr1 = A;             //重新置位指针
			pr2 = pr3;

			for(j=0; j<mLen; j++)
			{
				//A_In[j*nLen+i].real = A[j].real/mLen;           //将变换结果存入原矩阵中
				//A_In[j*nLen+i].image = A[j].image/mLen;

				pr2->real = pr1->real >> M;
				pr2->image = pr1->image >> M;
				pr1++;
				pr2 += nLen;
			}
		}
	}
	free(A);
}




