

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/atomic.h>


#include "rga_mmu_info.h"


extern rga_service_info rga_service;

#define KERNEL_SPACE_VALID    0xc0000000

static int rga_mem_size_cal(uint32_t Mem, uint32_t MemSize, uint32_t *StartAddr) 
{
    uint32_t start, end;
    uint32_t pageCount;

    end = (Mem + (MemSize + PAGE_SIZE - 1)) >> PAGE_SHIFT;
    start = Mem >> PAGE_SHIFT;
    pageCount = end - start;
    *StartAddr = start;
    return pageCount;    
}

static int rga_buf_size_cal(uint32_t yrgb_addr, uint32_t uv_addr, uint32_t v_addr, 
                                        int format, uint32_t w, uint32_t h, uint32_t *StartAddr ) 
{
    uint32_t size_yrgb = 0;
    uint32_t size_uv = 0;
    uint32_t size_v = 0;
    uint32_t stride = 0;
    uint32_t start, end;
    uint32_t pageCount;
       
    switch(format)
    {
        case RK_FORMAT_RGBA_8888 :
            stride = (w * 4 + 3) & (~3);
            size_yrgb = stride*h;
            end = (yrgb_addr + (size_yrgb + PAGE_SIZE - 1)) >> PAGE_SHIFT;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = end - start;            
            break;
        case RK_FORMAT_RGBX_8888 :
            stride = (w * 4 + 3) & (~3);
            size_yrgb = stride*h;
            end = (yrgb_addr + (size_yrgb + PAGE_SIZE - 1)) >> PAGE_SHIFT;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_RGB_888 :
            stride = (w * 3 + 3) & (~3);
            size_yrgb = stride*h;
            end = (yrgb_addr + (size_yrgb + PAGE_SIZE - 1)) >> PAGE_SHIFT;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_BGRA_8888 :
            size_yrgb = w*h*4;
            end = (yrgb_addr + (size_yrgb + PAGE_SIZE - 1)) >> PAGE_SHIFT;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = end - start;
            *StartAddr = start;
            break;
        case RK_FORMAT_RGB_565 :
            stride = (w*2 + 3) & (~3);            
            size_yrgb = stride * h;
            end = (yrgb_addr + (size_yrgb + PAGE_SIZE - 1)) >> PAGE_SHIFT;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_RGBA_5551 :
            stride = (w*2 + 3) & (~3);            
            size_yrgb = stride * h;
            end = (yrgb_addr + (size_yrgb + PAGE_SIZE - 1)) >> PAGE_SHIFT;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_RGBA_4444 :
            stride = (w*2 + 3) & (~3);            
            size_yrgb = stride * h;
            end = (yrgb_addr + (size_yrgb + PAGE_SIZE - 1)) >> PAGE_SHIFT;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_BGR_888 :
            stride = (w*3 + 3) & (~3);            
            size_yrgb = stride * h;
            end = (yrgb_addr + (size_yrgb + PAGE_SIZE - 1)) >> PAGE_SHIFT;
            start = yrgb_addr >> PAGE_SHIFT;
            pageCount = end - start;
            break;

        /* YUV FORMAT */
        case RK_FORMAT_YCbCr_422_SP :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = stride * h;
            start = MIN(yrgb_addr, uv_addr);
            start >>= PAGE_SHIFT; 
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;            
            break;
        case RK_FORMAT_YCbCr_422_P :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = ((stride >> 1) * h);
            size_v = ((stride >> 1) * h);
            start = MIN(MIN(yrgb_addr, uv_addr), v_addr);
            start = start >> PAGE_SHIFT; 
            end = MAX(MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv)), (v_addr + size_v));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;            
            break;
        case RK_FORMAT_YCbCr_420_SP :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = (stride * (h >> 1));            
            start = MIN(yrgb_addr, uv_addr);
            start >>= PAGE_SHIFT; 
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_YCbCr_420_P :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = ((stride >> 1) * (h >> 1));
            size_v = ((stride >> 1) * (h >> 1));
            start = MIN(MIN(yrgb_addr, uv_addr), v_addr);
            start >>= PAGE_SHIFT; 
            end = MAX(MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv)), (v_addr + size_v));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;            
            break;

        case RK_FORMAT_YCrCb_422_SP :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = stride * h;
            start = MIN(yrgb_addr, uv_addr);
            start >>= PAGE_SHIFT; 
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_YCrCb_422_P :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = ((stride >> 1) * h);
            size_v = ((stride >> 1) * h);
            start = MIN(MIN(yrgb_addr, uv_addr), v_addr);
            start >>= PAGE_SHIFT; 
            end = MAX(MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv)), (v_addr + size_v));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
            
        case RK_FORMAT_YCrCb_420_SP :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = (stride * (h >> 1));            
            start = MIN(yrgb_addr, uv_addr);
            start >>= PAGE_SHIFT; 
            end = MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        case RK_FORMAT_YCrCb_420_P :
            stride = (w + 3) & (~3);
            size_yrgb = stride * h;
            size_uv = ((stride >> 1) * (h >> 1));
            size_v = ((stride >> 1) * (h >> 1));
            start = MIN(MIN(yrgb_addr, uv_addr), v_addr);
            start >>= PAGE_SHIFT; 
            end = MAX(MAX((yrgb_addr + size_yrgb), (uv_addr + size_uv)), (v_addr + size_v));
            end = (end + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
            pageCount = end - start;
            break;
        #if 0    
        case RK_FORMAT_BPP1 :            
            break;
        case RK_FORMAT_BPP2 :
            break;
        case RK_FORMAT_BPP4 :
            break;
        case RK_FORMAT_BPP8 :
            break;
        #endif    
        default :
            pageCount = 0;
            start = 0;
            break;
    }

    *StartAddr = start;
    return pageCount;    
}

static int rga_MapUserMemory(struct page **pages, 
                                            uint32_t *pageTable, 
                                            uint32_t Memory, 
                                            uint32_t pageCount)
{
    int32_t result;
    uint32_t i;
    
    down_read(&current->mm->mmap_sem);
    result = get_user_pages(current,
            current->mm,
            Memory << PAGE_SHIFT,
            pageCount,
            1,
            0,
            pages,
            NULL
            );
    up_read(&current->mm->mmap_sem);
    
    if(result <= 0 || result < pageCount) 
    {
        return -EINVAL;                
    }

    for (i = 0; i < pageCount; i++)
    {
        /* Flush the data cache. */
#ifdef ANDROID
        dma_sync_single_for_device(
                    gcvNULL,
                    page_to_phys(pages[i]),
                    PAGE_SIZE,
                    DMA_TO_DEVICE);
#else
        flush_dcache_page(pages[i]);
#endif
    }

    /* Fill the page table. */
    for(i=0; i<pageCount; i++) {

        /* Get the physical address from page struct. */
        pageTable[i * (PAGE_SIZE/4096)] = page_to_phys(pages[i]);
    }    

    return 0;
}

static int rga_mmu_info_BitBlt_mode(struct rga_reg *reg, struct rga_req *req)
{    
    int SrcMemSize, DstMemSize, CMDMemSize;
    uint32_t SrcStart, DstStart, CMDStart;   
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base, *MMU_p;
    int ret;

    struct page **pages = NULL;

    /* cal src buf mmu info */                     
    SrcMemSize = rga_buf_size_cal(req->src.yrgb_addr, req->src.uv_addr, req->src.v_addr,
                                    req->src.format, req->src.vir_w, req->src.vir_h,
                                    &SrcStart);
    if(SrcMemSize == 0) {
        return -EINVAL;                
    }

    /* cal dst buf mmu info */    
    DstMemSize = rga_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                    req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                    &DstStart);
    if(DstMemSize == 0) {
        return -EINVAL; 
    }

    /* cal cmd buf mmu info */
    CMDMemSize = rga_mem_size_cal((uint32_t)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
    if(CMDMemSize == 0) {
        return -EINVAL; 
    }

    AllSize = SrcMemSize + DstMemSize + CMDMemSize;
               
    pages = (struct page **)kmalloc(AllSize * sizeof(struct page *), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc pages mem failed");
        return -EINVAL;                
    }
    
    MMU_Base = (uint32_t *)kmalloc(AllSize * sizeof(uint32_t), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc MMU_Base point failed");
        return -EINVAL;                
    }

    for(i=0; i<CMDMemSize; i++) {
        MMU_Base[i] = (uint32_t)virt_to_phys((uint32_t *)((CMDStart + i)<< PAGE_SHIFT));
    }

    if(req->src.yrgb_addr < KERNEL_SPACE_VALID)
    {
        ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], SrcStart, SrcMemSize);
        if (ret < 0) {
            pr_err("rga map src memory failed");
            return -EINVAL;
        }
    }
    else
    {
        MMU_p = MMU_Base + CMDMemSize;
        
        for(i=0; i<SrcMemSize; i++)
        {
            MMU_p[i] = (uint32_t)virt_to_phys((uint32_t *)((SrcStart + i) << PAGE_SHIFT));
        }
    }
    
    if(req->dst.yrgb_addr < KERNEL_SPACE_VALID)
    {
        ret = rga_MapUserMemory(&pages[CMDMemSize + SrcMemSize], &MMU_Base[CMDMemSize + SrcMemSize], DstStart, DstMemSize);
        if (ret < 0) {
            pr_err("rga map dst memory failed");
            return -EINVAL;
        }
    }
    else
    {
        MMU_p = MMU_Base + CMDMemSize + SrcMemSize;
        
        for(i=0; i<DstMemSize; i++)
        {
            MMU_p[i] = (uint32_t)virt_to_phys((uint32_t *)((DstStart + i) << PAGE_SHIFT));
        }       
    }

    /* zsq 
     * change the buf address in req struct
     * for the reason of lie to MMU 
     */
    req->mmu_info.base_addr = virt_to_phys(MMU_Base);
    
    req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);
    req->src.uv_addr = (req->src.uv_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);
    req->src.v_addr = (req->src.v_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);

    req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK)) | ((CMDMemSize + SrcMemSize) << PAGE_SHIFT);
    
    /*record the malloc buf for the cmd end to release*/
    reg->MMU_base = MMU_Base;

    if (pages != NULL) {
        /* Free the page table */
        kfree(pages);
    }        

    return 0;
}

static int rga_mmu_info_color_palette_mode(struct rga_reg *reg, struct rga_req *req)
{
    int SrcMemSize, DstMemSize, CMDMemSize;
    uint32_t SrcStart, DstStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base;
    int ret;
    uint32_t stride;

    uint8_t shift;
    uint16_t sw, byte_num;
    
    shift = 3 - (req->palette_mode & 3);
    sw = req->src.vir_w;
    byte_num = sw >> shift;
    stride = (byte_num + 3) & (~3);
                         
    SrcMemSize = rga_mem_size_cal(req->src.yrgb_addr, stride, &SrcStart);
    if(SrcMemSize == 0) {
        return -EINVAL;                
    }

    DstMemSize = rga_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                    req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                    &DstStart);
    if(DstMemSize == 0) {
        return -EINVAL; 
    }

    CMDMemSize = rga_mem_size_cal((uint32_t)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
    if(CMDMemSize == 0) {
        return -EINVAL; 
    }

    AllSize = SrcMemSize + DstMemSize + CMDMemSize;
               
    pages = (struct page **)kmalloc(AllSize * sizeof(struct page *), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc pages mem failed");
        return -EINVAL;                
    }
    
    MMU_Base = (uint32_t *)kmalloc(AllSize * sizeof(uint32_t), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc MMU_Base point failed");
        return -EINVAL;                
    }

    for(i=0; i<CMDMemSize; i++) {
        MMU_Base[i] = virt_to_phys((uint32_t *)((CMDStart + i)<<PAGE_SHIFT));
    }
                
    ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], SrcStart, SrcMemSize);
    if (ret < 0) {
        pr_err("rga map src memory failed");
        return -EINVAL;
    }

    ret = rga_MapUserMemory(&pages[CMDMemSize + SrcMemSize], &MMU_Base[CMDMemSize + SrcMemSize], DstStart, DstMemSize);
    if (ret < 0) {
        pr_err("rga map dst memory failed");
        return -EINVAL;
    }

    /* zsq 
     * change the buf address in req struct
     * for the reason of lie to MMU 
     */
    req->mmu_info.base_addr = virt_to_phys(MMU_Base);    
    req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);
    req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK)) | ((CMDMemSize + SrcMemSize) << PAGE_SHIFT);

    
    /*record the malloc buf for the cmd end to release*/
    reg->MMU_base = MMU_Base;

    if (pages != NULL) {
        /* Free the page table */
        kfree(pages);
    }        

    return 0;
}

static int rga_mmu_info_color_fill_mode(struct rga_reg *reg, struct rga_req *req)
{
    int DstMemSize, CMDMemSize;
    uint32_t DstStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base;
    int ret;
                         
    DstMemSize = rga_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                    req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                    &DstStart);
    if(DstMemSize == 0) {
        return -EINVAL; 
    }

    CMDMemSize = rga_mem_size_cal((uint32_t)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
    if(CMDMemSize == 0) {
        return -EINVAL; 
    }

    AllSize = DstMemSize + CMDMemSize;
               
    pages = (struct page **)kmalloc(AllSize * sizeof(struct page *), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc pages mem failed");
        return -EINVAL;                
    }
    
    MMU_Base = (uint32_t *)kmalloc(AllSize * sizeof(uint32_t), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc MMU_Base point failed");
        return -EINVAL;                
    }

    for(i=0; i<CMDMemSize; i++) {
        MMU_Base[i] = virt_to_phys((uint32_t *)((CMDStart+i)<<PAGE_SHIFT));
    }
                
    ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], DstStart, DstMemSize);
    if (ret < 0) {
        pr_err("rga map dst memory failed");
        return -EINVAL;
    }

    /* zsq 
     * change the buf address in req struct
     * for the reason of lie to MMU 
     */
    req->mmu_info.base_addr = virt_to_phys(MMU_Base);    
    req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK)) | ((CMDMemSize) << PAGE_SHIFT);
   
    
    /*record the malloc buf for the cmd end to release*/
    reg->MMU_base = MMU_Base;

    if (pages != NULL) {
        /* Free the page table */
        kfree(pages);
    }       

    return 0;
}


static int rga_mmu_info_line_point_drawing_mode(struct rga_reg *reg, struct rga_req *req)
{
    int DstMemSize, CMDMemSize;
    uint32_t DstStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base;
    int ret;

    /* cal dst buf mmu info */                     
    DstMemSize = rga_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                    req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                    &DstStart);
    if(DstMemSize == 0) {
        return -EINVAL; 
    }

    CMDMemSize = rga_mem_size_cal((uint32_t)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
    if(CMDMemSize == 0) {
        return -EINVAL; 
    }

    AllSize = DstMemSize + CMDMemSize;
               
    pages = (struct page **)kmalloc(AllSize * sizeof(struct page *), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc pages mem failed");
        return -EINVAL;                
    }
    
    MMU_Base = (uint32_t *)kmalloc(AllSize * sizeof(uint32_t), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc MMU_Base point failed");
        return -EINVAL;                
    }

    for(i=0; i<CMDMemSize; i++) {
        MMU_Base[i] = virt_to_phys((uint32_t *)((CMDStart+i)<<PAGE_SHIFT));
    }
                
    ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], DstStart, DstMemSize);
    if (ret < 0) {
        pr_err("rga map dst memory failed");
        return -EINVAL;
    }

    /* zsq 
     * change the buf address in req struct
     * for the reason of lie to MMU 
     */
    req->mmu_info.base_addr = virt_to_phys(MMU_Base);    
    req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK)) | ((CMDMemSize) << PAGE_SHIFT);
   
    
    /*record the malloc buf for the cmd end to release*/
    reg->MMU_base = MMU_Base;

    if (pages != NULL) {
        /* Free the page table */
        kfree(pages);
    } 

    return 0;
}

static int rga_mmu_info_blur_sharp_filter_mode(struct rga_reg *reg, struct rga_req *req)
{
    int SrcMemSize, DstMemSize, CMDMemSize;
    uint32_t SrcStart, DstStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base;
    int ret;

    /* cal src buf mmu info */                     
    SrcMemSize = rga_buf_size_cal(req->src.yrgb_addr, req->src.uv_addr, req->src.v_addr,
                                    req->src.format, req->src.vir_w, req->src.vir_h,
                                    &SrcStart);
    if(SrcMemSize == 0) {
        return -EINVAL;                
    }

    /* cal dst buf mmu info */    
    DstMemSize = rga_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                    req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                    &DstStart);
    if(DstMemSize == 0) {
        return -EINVAL; 
    }

    /* cal cmd buf mmu info */
    CMDMemSize = rga_mem_size_cal((uint32_t)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
    if(CMDMemSize == 0) {
        return -EINVAL; 
    }

    AllSize = SrcMemSize + DstMemSize + CMDMemSize;
               
    pages = (struct page **)kmalloc(AllSize * sizeof(struct page *), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc pages mem failed");
        return -EINVAL;                
    }
    
    MMU_Base = (uint32_t *)kmalloc(AllSize * sizeof(uint32_t), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc MMU_Base point failed");
        return -EINVAL;                
    }

    for(i=0; i<CMDMemSize; i++) {
        MMU_Base[i] = virt_to_phys((uint32_t *)((CMDStart + i)<< PAGE_SHIFT));
    }
                
    ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], SrcStart, SrcMemSize);
    if (ret < 0) {
        pr_err("rga map src memory failed");
        return -EINVAL;
    }

    ret = rga_MapUserMemory(&pages[CMDMemSize + SrcMemSize], &MMU_Base[CMDMemSize + SrcMemSize], DstStart, DstMemSize);
    if (ret < 0) {
        pr_err("rga map dst memory failed");
        return -EINVAL;
    }

    /* zsq 
     * change the buf address in req struct
     * for the reason of lie to MMU 
     */
    req->mmu_info.base_addr = virt_to_phys(MMU_Base);
    
    req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);
    req->src.uv_addr = (req->src.uv_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);
    req->src.v_addr = (req->src.v_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);

    req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK)) | ((CMDMemSize + SrcMemSize) << PAGE_SHIFT);
    
    /*record the malloc buf for the cmd end to release*/
    reg->MMU_base = MMU_Base;

    if (pages != NULL) {
        /* Free the page table */
        kfree(pages);
    }  

    return 0;
}



static int rga_mmu_info_pre_scale_mode(struct rga_reg *reg, struct rga_req *req)
{
    int SrcMemSize, DstMemSize, CMDMemSize;
    uint32_t SrcStart, DstStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base, *MMU_p;
    int ret;

    /* cal src buf mmu info */                     
    SrcMemSize = rga_buf_size_cal(req->src.yrgb_addr, req->src.uv_addr, req->src.v_addr,
                                    req->src.format, req->src.vir_w, req->src.vir_h,
                                    &SrcStart);
    if(SrcMemSize == 0) {
        return -EINVAL;                
    }

    /* cal dst buf mmu info */    
    DstMemSize = rga_buf_size_cal(req->dst.yrgb_addr, req->dst.uv_addr, req->dst.v_addr,
                                    req->dst.format, req->dst.vir_w, req->dst.vir_h,
                                    &DstStart);
    if(DstMemSize == 0) {
        return -EINVAL; 
    }

    /* cal cmd buf mmu info */
    CMDMemSize = rga_mem_size_cal((uint32_t)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
    if(CMDMemSize == 0) {
        return -EINVAL; 
    }

    AllSize = SrcMemSize + DstMemSize + CMDMemSize;
               
    pages = (struct page **)kmalloc(AllSize * sizeof(struct page *), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc pages mem failed");
        return -EINVAL;                
    }

    /* 
     * Allocate MMU Index mem
     * This mem release in run_to_done fun 
     */
    MMU_Base = (uint32_t *)kmalloc(AllSize * sizeof(uint32_t), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc MMU_Base point failed");
        return -EINVAL;                
    }

    for(i=0; i<CMDMemSize; i++) {
        MMU_Base[i] = virt_to_phys((uint32_t *)((CMDStart + i) << PAGE_SHIFT));
    }


    /* map src pages */
    ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], SrcStart, SrcMemSize);
    if (ret < 0) {
        pr_err("rga map src memory failed");
        return -EINVAL;
    }

    
    if(req->dst.yrgb_addr >= 0xc0000000) 
    {   
        /* kernel space */
        MMU_p = MMU_Base + CMDMemSize + SrcMemSize;
        for(i=0; i<DstMemSize; i++) 
        {
            MMU_p[i] = virt_to_phys((uint32_t *)((DstStart + i)<< PAGE_SHIFT));        
        }
    }
    else 
    {
        /* user space */
        ret = rga_MapUserMemory(&pages[CMDMemSize + SrcMemSize], &MMU_Base[CMDMemSize + SrcMemSize], DstStart, DstMemSize);
        if (ret < 0) 
        {
            pr_err("rga map dst memory failed");
            return -EINVAL;
        }        
    }

    /* zsq 
     * change the buf address in req struct
     * for the reason of lie to MMU 
     */
    req->mmu_info.base_addr = virt_to_phys(MMU_Base);
    
    req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);
    req->src.uv_addr = (req->src.uv_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);
    req->src.v_addr = (req->src.v_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);

    req->dst.yrgb_addr = (req->dst.yrgb_addr & (~PAGE_MASK)) | ((CMDMemSize + SrcMemSize) << PAGE_SHIFT);
    
    /*record the malloc buf for the cmd end to release*/
    reg->MMU_base = MMU_Base;

    if (pages != NULL) {
        /* Free the page table */
        kfree(pages);
    }  

    return 0;
}


static int rga_mmu_info_update_palette_table_mode(struct rga_reg *reg, struct rga_req *req)
{
    int SrcMemSize, DstMemSize, CMDMemSize;
    uint32_t SrcStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base;
    int ret;

    /* cal src buf mmu info */                     
    SrcMemSize = rga_mem_size_cal(req->src.yrgb_addr, req->src.vir_w * req->src.vir_h, &SrcStart);
    if(SrcMemSize == 0) {
        return -EINVAL;                
    }

    /* cal cmd buf mmu info */
    CMDMemSize = rga_mem_size_cal((uint32_t)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
    if(CMDMemSize == 0) {
        return -EINVAL; 
    }

    AllSize = SrcMemSize + DstMemSize + CMDMemSize;
               
    pages = (struct page **)kmalloc(AllSize * sizeof(struct page *), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc pages mem failed");
        return -EINVAL;                
    }
    
    MMU_Base = (uint32_t *)kmalloc(AllSize * sizeof(uint32_t), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc MMU_Base point failed");
        return -EINVAL;                
    }

    for(i=0; i<CMDMemSize; i++) {
        MMU_Base[i] = virt_to_phys((uint32_t *)((CMDStart + i) << PAGE_SHIFT));
    }
                
    ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], SrcStart, SrcMemSize);
    if (ret < 0) {
        pr_err("rga map src memory failed");
        return -EINVAL;
    }

    /* zsq 
     * change the buf address in req struct
     * for the reason of lie to MMU 
     */
    req->mmu_info.base_addr = virt_to_phys(MMU_Base);
    
    req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);    
    
    /*record the malloc buf for the cmd end to release*/
    reg->MMU_base = MMU_Base;

    if (pages != NULL) {
        /* Free the page table */
        kfree(pages);
    }  

    return 0;
}

static int rga_mmu_info_update_patten_buff_mode(struct rga_reg *reg, struct rga_req *req)
{
    int SrcMemSize, DstMemSize, CMDMemSize;
    uint32_t SrcStart, CMDStart;
    struct page **pages = NULL;
    uint32_t i;
    uint32_t AllSize;
    uint32_t *MMU_Base;
    int ret;

    /* cal src buf mmu info */                     
    SrcMemSize = rga_mem_size_cal(req->pat.yrgb_addr, req->pat.vir_w * req->pat.vir_h * 4, &SrcStart);
    if(SrcMemSize == 0) {
        return -EINVAL;                
    }

    /* cal cmd buf mmu info */
    CMDMemSize = rga_mem_size_cal((uint32_t)rga_service.cmd_buff, RGA_CMD_BUF_SIZE, &CMDStart);
    if(CMDMemSize == 0) {
        return -EINVAL; 
    }

    AllSize = SrcMemSize + DstMemSize + CMDMemSize;
               
    pages = (struct page **)kmalloc(AllSize * sizeof(struct page *), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc pages mem failed");
        return -EINVAL;                
    }
    
    MMU_Base = (uint32_t *)kmalloc(AllSize * sizeof(uint32_t), GFP_KERNEL);
    if(pages == NULL) {
        pr_err("RGA MMU malloc MMU_Base point failed");
        return -EINVAL;                
    }

    for(i=0; i<CMDMemSize; i++) {
        MMU_Base[i] = virt_to_phys((uint32_t *)((CMDStart + i) << PAGE_SHIFT));
    }
                
    ret = rga_MapUserMemory(&pages[CMDMemSize], &MMU_Base[CMDMemSize], SrcStart, SrcMemSize);
    if (ret < 0) {
        pr_err("rga map src memory failed");
        return -EINVAL;
    }

    /* zsq 
     * change the buf address in req struct
     * for the reason of lie to MMU 
     */
    req->mmu_info.base_addr = virt_to_phys(MMU_Base);
    
    req->src.yrgb_addr = (req->src.yrgb_addr & (~PAGE_MASK)) | (CMDMemSize << PAGE_SHIFT);    
    
    /*record the malloc buf for the cmd end to release*/
    reg->MMU_base = MMU_Base;

    if (pages != NULL) {
        /* Free the page table */
        kfree(pages);
    }

    return 0;
}

int rga_set_mmu_info(struct rga_reg *reg, struct rga_req *req)
{    
    int ret;
               
    switch (req->render_mode) {
        case bitblt_mode :            
            ret = rga_mmu_info_BitBlt_mode(reg, req);
            break;
        case color_palette_mode :
            ret = rga_mmu_info_color_palette_mode(reg, req);
            break;
        case color_fill_mode :
            ret = rga_mmu_info_color_fill_mode(reg, req);
            break;
        case line_point_drawing_mode :
            ret = rga_mmu_info_line_point_drawing_mode(reg, req);
            break;
        case blur_sharp_filter_mode :
            ret = rga_mmu_info_blur_sharp_filter_mode(reg, req);
            break;
        case pre_scaling_mode :
            ret = rga_mmu_info_pre_scale_mode(reg, req);
            break;
        case update_palette_table_mode :
            ret = rga_mmu_info_update_palette_table_mode(reg, req);
            break;
        case update_patten_buff_mode :
            ret = rga_mmu_info_update_patten_buff_mode(reg, req);
            break;
        default :
            ret = -1;
            break;
    }

    return ret;
}

