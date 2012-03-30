#ifndef _RK29_IPP_DRIVER_H_
#define _RK29_IPP_DRIVER_H_


#define IPP_BLIT_SYNC	0x5017
#define IPP_BLIT_ASYNC  0x5018
#define IPP_GET_RESULT  0x5019


/* Image data */
struct rk29_ipp_image
{
	uint32_t	YrgbMst;	// image Y/rgb address
	uint32_t    CbrMst;	// image CbCr address
	uint32_t	w;	// image full width
	uint32_t	h;	// image full height
	uint32_t	fmt;	// color format
};

struct rk29_ipp_req {
	struct rk29_ipp_image src0; // source0 image
	struct rk29_ipp_image dst0; // destination0 image
	//struct rk29_ipp_image src1; // source1 image
	//struct rk29_ipp_image dst1; // destination1 image
	uint32_t src_vir_w;
	uint32_t dst_vir_w;
	uint32_t timeout;
	
	uint32_t flag; //rotate

	/*store_clip_mode 
	    0:when src width is not 64-bits aligned,use dummy data make it 64-bits aligned  1:packed
	   we usually set to 0
	*/
	uint8_t store_clip_mode;
	
	//deinterlace_enable  0:disable 1:enable 2:query
	uint8_t deinterlace_enable;
	//the sum of three paras should be 32,and single para should be less than 32
	uint8_t deinterlace_para0;
	uint8_t deinterlace_para1;
	uint8_t deinterlace_para2;
	
	/* completion is reported through a callback */
	void			(*complete)(int retval);
		
};

//format enum
enum
{
	IPP_XRGB_8888 = 0,
	IPP_RGB_565 =1 ,
	IPP_Y_CBCR_H2V1 = 2,  //yuv 422sp
	IPP_Y_CBCR_H2V2 = 3, //yuv 420sp
	IPP_Y_CBCR_H1V1 =6, //yuv 444sp
	IPP_IMGTYPE_LIMIT
};

typedef enum
 {
     IPP_ROT_90,
     IPP_ROT_180,
     IPP_ROT_270,
     IPP_ROT_X_FLIP,
     IPP_ROT_Y_FLIP,
     IPP_ROT_0,
     IPP_ROT_LIMIT
 } ROT_DEG;

 struct ipp_regs {
	uint32_t ipp_config;
	uint32_t ipp_src_img_info;
	uint32_t ipp_dst_img_info;
	uint32_t ipp_img_vir;
	uint32_t ipp_int;
	uint32_t ipp_src0_y_mst;
	uint32_t ipp_src0_Cbr_mst;
	uint32_t ipp_src1_y_mst;
	uint32_t ipp_src1_Cbr_mst;
	uint32_t ipp_dst0_y_mst;
	uint32_t ipp_dst0_Cbr_mst;
	uint32_t ipp_dst1_y_mst;
	uint32_t ipp_dst1_Cbr_mst;
	uint32_t ipp_pre_scl_para;
	uint32_t ipp_post_scl_para;
	uint32_t ipp_swap_ctrl;
	uint32_t ipp_pre_img_info;
	uint32_t ipp_axi_id;
	uint32_t ipp_process_st;
};

#define IPP_CONFIG 				(0x00)
#define IPP_SRC_IMG_INFO 		(0x04)
#define IPP_DST_IMG_INFO 		(0x08)
#define IPP_IMG_VIR				(0x0c)
#define IPP_INT					(0x10)
#define IPP_SRC0_Y_MST			(0x14)
#define IPP_SRC0_CBR_MST		(0x18)
#define IPP_SRC1_Y_MST			(0x1c)
#define IPP_SRC1_CBR_MST		(0x20)
#define IPP_DST0_Y_MST			(0x24)
#define IPP_DST0_CBR_MST		(0x28)
#define IPP_DST1_Y_MST			(0x2c)
#define IPP_DST1_CBR_MST		(0x30)
#define IPP_PRE_SCL_PARA		(0x34)
#define IPP_POST_SCL_PARA		(0x38)
#define IPP_SWAP_CTRL			(0x3c)
#define IPP_PRE_IMG_INFO		(0x40)
#define IPP_AXI_ID				(0x44)
#define IPP_SRESET				(0x48)
#define IPP_PROCESS_ST			(0x50)

/*ipp config*/
#define STORE_CLIP_MODE			(1<<26)
#define DEINTERLACE_ENABLE		(1<<24)
#define ROT_ENABLE				(1<<8)
#define PRE_SCALE				(1<<4)
#define POST_SCALE				(1<<3)

#define IPP_BLIT_COMPLETE_EVENT BIT(1)

#define IS_YCRCB(img) ((img == IPP_Y_CBCR_H2V1) | (img == IPP_Y_CBCR_H2V2) | \
		       (img == IPP_Y_CBCR_H1V1) )
#define IS_RGB(img) ((img == IPP_RGB_565) | (img == IPP_ARGB_8888) | \
		     (img == IPP_XRGB_8888) ))
#define HAS_ALPHA(img) (img == IPP_ARGB_8888)


int ipp_blit_async(const struct rk29_ipp_req *req);
//int ipp_blit_sync(const struct rk29_ipp_req *req);
extern int (*ipp_blit_sync)(const struct rk29_ipp_req *req);
#endif /*_RK29_IPP_DRIVER_H_*/