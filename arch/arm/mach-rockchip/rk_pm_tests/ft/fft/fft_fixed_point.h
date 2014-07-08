#ifndef __FFT_FIXED_POINT_H_
#define __FFT_FIXED_POINT_H_


#define DXT_FORWARD  0
#define DXT_INVERSE  1

#define Q_INPUT 8

typedef struct{
	int real;
	int image;
}RK_complex_INT;


//2012-05-07
//以下是fft变换的相关函数
//序列的逆序排列
void reverse(int len, int M,int *b);
int nexttopow2(int x);
void fft_fixed_point(int fft_nLen, int fft_M, RK_complex_INT * A);
void fft_2D_fixed_point(int mLen,int nLen,int M,int N,RK_complex_INT *A_In,int flag);


#endif