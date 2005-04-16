/*
 * $Id: btcx-risc.h,v 1.2 2004/09/15 16:15:24 kraxel Exp $
 */
struct btcx_riscmem {
	unsigned int   size;
	u32            *cpu;
	u32            *jmp;
	dma_addr_t     dma;
};

struct btcx_skiplist {
	int start;
	int end;
};

int  btcx_riscmem_alloc(struct pci_dev *pci,
			struct btcx_riscmem *risc,
			unsigned int size);
void btcx_riscmem_free(struct pci_dev *pci,
		       struct btcx_riscmem *risc);

int btcx_screen_clips(int swidth, int sheight, struct v4l2_rect *win,
		      struct v4l2_clip *clips, unsigned int n);
int btcx_align(struct v4l2_rect *win, struct v4l2_clip *clips,
	       unsigned int n, int mask);
void btcx_sort_clips(struct v4l2_clip *clips, unsigned int nclips);
void btcx_calc_skips(int line, int width, unsigned int *maxy,
		     struct btcx_skiplist *skips, unsigned int *nskips,
		     const struct v4l2_clip *clips, unsigned int nclips);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
