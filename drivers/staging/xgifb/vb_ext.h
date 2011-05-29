#ifndef  _VBEXT_
#define  _VBEXT_

struct DWORDREGS {
    unsigned long    Eax, Ebx, Ecx, Edx, Esi, Edi, Ebp;
};

struct WORDREGS {
    unsigned short ax, hi_ax, bx, hi_bx, cx, hi_cx, dx, hi_dx, si,
	    hi_si, di, hi_di, bp, hi_bp;
};

struct BYTEREGS {
     unsigned char al, ah, hi_al, hi_ah, bl, bh, hi_bl, hi_bh, cl, ch,
	     hi_cl, hi_ch, dl, dh, hi_dl, hi_dh;
};

typedef union   _X86_REGS    {
    struct  DWORDREGS e;
    struct  WORDREGS x;
    struct  BYTEREGS h;
} X86_REGS, *PX86_REGS;

extern   void     XGI_GetSenseStatus(struct xgi_hw_device_info *HwDeviceExtension, struct vb_device_info *pVBInfo);
extern unsigned short XGINew_SenseLCD(struct xgi_hw_device_info *,
				      struct vb_device_info *pVBInfo);

#endif
