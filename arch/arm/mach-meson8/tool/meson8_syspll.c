/*
 *	Create MESON8 SYSPLL Frequency Table
 *
 *	By Victor Wan <victor.wan@amlogic.com>
 *	First version: 2013.9.17
 *	V2 : 2013.9.22
 *				1) fvco min --> 1.200GHz
 *				2) fix some wrong pll config
 *
 *	gcc -o meson8_syspll meson8_syspll.c
 *	meson8_syspll
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

//#define DEBUG 1

#define FVCO_MAX 2550000
#define FVCO_MIN 1200000

#define F_MAX 2112000
#define F_MIN 24000

#define TABLE_CLM	4 /* freq, cntl, latency, fvco */

const int cntl0_init_base = 0x40000200;
const int od_base = (1 << 16);
const int crystal = 24000;

#ifdef DEBUG
#define debug  printf
#else
#define debug
#endif

int calc_fvco(int m,int n)
{
	int f;

	f = crystal * m /n;
	return f;
}

int calc_fout(int freq,int od,int ext_div)
{
	int f, div;
	
	f = freq;
	if(od == 0)
		div = 1;
	else
		div = od * 2;
		
	f = f / div;
	if(ext_div == 0)
		return f;
	else{
		f = f / 2;
		return f / (ext_div + 1);
	}
}
int calc_cntl(int freq, int m, int od)
{
		return (cntl0_init_base + od_base * od + m);
}
int get_od_from_cntl(int cntl)
{
	return ((cntl & (od_base * 3))  / od_base);
}
int get_ext_div_from_latency(int latency)
{
	return ((latency & 0x03ff0000) >> 16);
}
int calc_latency(int freq, int ext_div, int * l2, int * axi, int * peri, int * apb)
{
	if(freq < F_MAX){
		*l2 = 3;
		*axi = 5;
		*peri = 4;
		*apb = 6;
	}
	return (ext_div << 16) + (*l2 << 12) + (*axi << 8) + (*peri << 4) + *apb;
}

int add_freq_to_table(int* table, int freq, int fvco, int m, int od, int ext_div)
{
	int ret = 0;
	int k = 0;
	int exist = 0;
	int record = 0;
	int vco = fvco;
	int* f = table;
	int l2,axi,peri,apb;
	int f_cntl,f_latency,f_fvco;
	
	while(f[0]){
		if(f[1]){ 
			record++;
		}

		if(f[0] == freq){
			
			f_cntl = calc_cntl(freq, m, od);
			f_latency = calc_latency(freq,ext_div,&l2,&axi,&peri,&apb);
			f_fvco = fvco;
			
			if(f[1]) // record exist
				exist = 1;
			else{
				f[1] = f_cntl;
				f[2] = f_latency;
				f[3] = f_fvco;
				return 0;
			}
		}
		
		k++;
		f += TABLE_CLM;
	}
	if(k == record)
		return -2; //full
		
	if(exist)
		return -1;
		
	printf("Strange !!!! -- freq: %d, fvco: %d, m: %d, od: %d, ext_div: %d, k: %d , record: %d\n",
			freq, fvco, m, od, ext_div, k, record);
		
	//never go here
	return 0;
}


int show_freq(int * table)
{
	int i;
	int latency;
	int od,k;
	while(table[0]){
		od = get_od_from_cntl(table[1]);
		switch(od){
			case 0:
				k = 1;break;
			case 1:
				k = 2;break;
			case 2:
				k = 4;break;
			default:
				k = 999; // error
		}
		latency = get_ext_div_from_latency(table[2]);
		printf("\t\t{ %4d,",table[0]/1000);//Freq
		printf(" 0x%08X,",table[1]);//cntl0
		printf(" 0x%08X },",table[2]);//latency
		printf(" /* fvco %4d, / %d, /%2d */",
			table[3]/1000,k,latency ?((latency + 1) * 2): 1 );//fvco
		printf("\n");
		table+=TABLE_CLM;
	}
}

int * init_freq_table(int min, int max, int step)
{
	int * p;
	int * table;
	int size =  TABLE_CLM * ((max - min) / step + 2) * sizeof(int);
	int freq;
	
	p = malloc(size);
	if(p){
		memset(p,0,size);
		table = (int *)p;
		for(freq = min; freq <= max; freq+= step, table += TABLE_CLM)
			table[0] = freq;
	}
	table[0] = 0;
	
	return p;
}
int main(void)
{
	int m,od,ext_div;
	int fvco,fout;
	int ret;
	int * freq_table = NULL;
	int flag;
	
	freq_table = init_freq_table(F_MIN, F_MAX, crystal);

#ifdef DEBUG	
	printf(" === Freq table before ===\n");
	show_freq(freq_table);
#endif

	for(ext_div = 0; ext_div < 1024; ext_div++){
		for(od = 0; od <= 2; od++){
				for(m = 1; m < 512; m++){

					fvco = calc_fvco(m, 1);

					if(fvco < FVCO_MIN || fvco > FVCO_MAX){
						continue;
					}

  				fout = calc_fout(fvco, od, ext_div);
					debug("--- fvco: %d, fout: %d, od: %d, m: %d, ext_div: %d\n",fvco,fout, od, m, ext_div);
  				if(fout < F_MIN || fout > F_MAX){
 						debug("=== skip fout %d\n",fout);
  					continue;
					} 
 					debug("--- fout: %d, od: %d, m: %d, ext_div: %d\n",fout,od, m, ext_div);
  				if((fout / (crystal / 1000)) != ((fout / crystal) * 1000))
  					continue;
  				
  				/* get a poper fout */
  				debug("=== found fvco %d, fout %d, m %d, od %d, ext_div, %d\n",
  					fvco,fout,m, od, ext_div);
  				ret = add_freq_to_table(freq_table, fout, fvco, m, od, ext_div);
  				if(ret == 0)// Add successful
  					continue;
  				if(ret == -1)// Alread have this item
  					continue;
  				if(ret == -2)
  					goto FINISH;

			}		
		}
	}

FINISH:
	printf(" ======= Meson8 syspll freq table  =======\n");
	show_freq(freq_table);
	printf(" ======= Meson8 syspll freq table finished =======\n");
	
	free(freq_table);
	return 0;
}