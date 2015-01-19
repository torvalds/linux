#include <linux/string.h>
#include <linux/amlogic/efuse.h>
#include <linux/slab.h>

static inline int cm(int p, int x)
{
	int i, tmp;

	tmp = x;
	for (i=0; i<p; i++) {
		tmp <<= 1;
		if (tmp&(1<<8)) tmp ^= 0x11d;
	}

	return tmp;
}

static inline int gm(int x, int y)
{
	int i, tmp;

	tmp = 0;
	for (i=0; i<8; i++) {
		if (y&(1<<i)) tmp ^= cm(i, x);
	}

	return tmp;
}

static void bch_enc(int c[255], int n, int t)
{
	int i, j, r, gate;
	int b[20], g[20];
	char gs1[] = "101110001";
	char gs2[] = "11000110111101101";

	memset(g, 0, 20*sizeof(int));
	memset(b, 0, 20*sizeof(int));
	r = t*8;

	for (i=0; i<r+1; i++) {
		g[i] = (t==1 ? gs1[i] : gs2[i]) - '0';
	}

	for (i=0; i<n; i++) {
		c[i] = i<n-r ? c[i] : b[r-1];
		gate = i<n-r ? c[i]^b[r-1] : 0;
		for (j=r-1; j>=0; j--)
		b[j] = gate*g[j] ^ (j==0?0:b[j-1]);
	}
}

static int bch_dec(int c[255], int n, int t)
{
	int i, j, tmp;
	int s[4], a[6], b[6], temp[6], deg_a, deg_b;
	int sig[5], deg_sig;
	int errloc[4], errcnt;

	memset(s, 0, 4*sizeof(int));
	for (j=0; j<n; j++)
		for (i=0; i<t*2; i++)
			s[i] = cm(i+1, s[i]) ^ (c[j]&1);

	memset(a, 0, 6*sizeof(int));
	memset(b, 0, 6*sizeof(int));
	a[0] = 1;
	deg_a = t*2;
	for (i=0; i<t*2; i++) b[i] = s[t*2-1-i];
		deg_b = t*2-1;

	a[t*2+1] = 1;
	b[t*2+1] = 0;
	while (deg_b >= t) {
		if (b[0] == 0) {
			memmove(b, b+1, 5*sizeof(int));
			b[5] = 0;
			deg_b--;
        }
        else {
			for (i=t*2+1; i>deg_a; i--)
				b[i] = gm(b[i], b[0]) ^ gm(a[i], a[0]);
			for (i=deg_a; i>deg_b; i--) {
				b[i] = gm(b[i], b[0]);
				a[i] = gm(a[i], b[0]);
			}
			for (; i>0; i--)
				a[i] = gm(a[i], b[0]) ^ gm(b[i], a[0]);
				memmove(a, a+1, 5*sizeof(int));
				a[5] = 0;
				deg_a--;

			if (deg_a < deg_b) {
				memcpy(temp, a, 6*sizeof(int));
				memcpy(a, b, 6*sizeof(int));
				memcpy(b, temp, 6*sizeof(int));

				tmp = deg_a;
				deg_a = deg_b;
				deg_b = tmp;
			}
		}
	}

	deg_sig = t*2 - deg_a;
	memcpy(sig, a+deg_a+1, (deg_sig+1)*sizeof(int));

	errcnt = 0;
	for (j=0; j<255; j++) {
		tmp = 0;
		for (i=0; i<=deg_sig; i++) {
			sig[i] = cm(i, sig[i]);
			tmp ^= sig[i];
		}

		if (tmp == 0) {
			errloc[errcnt] = j - (255-n);
			if (errloc[errcnt] >= 0)
			errcnt++;
		}
	}

	if (errcnt<deg_sig) {
		return -1;
	}

	for (i=0; i<errcnt; i++) {
		c[errloc[i]] ^= 1;
		__D("fix error at %4d\n", errloc[i]);
	}

	return errcnt;
}


int efuse_bch_enc(const char *ibuf, int isize, char *obuf, int reverse)
{
	int i, j;
	int tmp;
	//int errnum, errbit;
	char info;
	//int c[255];
	int t = BCH_T;
	int n = isize*8 + t*8;
	int *c=NULL;
	c=kmalloc(sizeof(int) * 255, GFP_KERNEL);
	if(!c){
		printk("malloc buffer error for efuse\n");
		return -1;
	}
	memset(c, 0, sizeof(int) * 255);
	

	for (i = 0; i < isize; ++i) {
		info = ibuf[i];
		if(reverse)
			info = ~info;
		for (j = 0; j < 8; ++j) {
			c[i*8 + j] = info >> (7 - j)&1;
		}
	}

	bch_enc(c, n, t);

#ifdef __ADDERR
	/* add error */
	errnum = t;
	for ( i = 0; i < errnum; ++i) {
		errbit = rand()%n;
		c[errbit] ^= 1;
		__D("add error #%d at %d\n", i, errbit );
	}
#endif

	for (i = 0; i < n/8; ++i) {
		tmp = 0;
		for (j = 0; j < 8; ++j) {
			tmp += c[i*8 + j]<<(7-j);
		}
		
		if(reverse)
			obuf[i] = ~tmp;
		else
			obuf[i] = tmp;
	}
	kfree(c);
	return 0;
}

int efuse_bch_dec(const char *ibuf, int isize, char *obuf, int reverse)
{
	int i, j;
	int tmp;
	char info;
	//int c[255];
	int t = BCH_T;
	int n = isize*8;
	int *c=NULL;
	c=kmalloc(sizeof(int) * 255, GFP_KERNEL);
	if(!c){
		printk("malloc buffer error for efuse\n");
		return -1;
	}
	memset(c, 0, sizeof(int) * 255);

	for (i = 0; i < isize; ++i) {
		info = ibuf[i];
		if(reverse)
			info = ~info;
		for (j = 0; j < 8; ++j) {
			c[i*8 + j] = info >> (7 - j)&1;
		}
	}

	bch_dec(c, n, t);

	for (i = 0; i < (n/8 - t); ++i) {
		tmp = 0;
		for (j = 0; j < 8; ++j) {
			tmp += c[i*8 + j]<<(7-j);
		}
		if(reverse)
			obuf[i] = ~tmp;
		else
			obuf[i] = tmp;
	}
	kfree(c);
	return 0;
}
