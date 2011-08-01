/*
 * function for test and debug
 *
 * Copyright (C) 2011 Rochchip, Inc.
 * Author: Hsl
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  relative files: drivers/dbg/ , arch/arm/kernel/entry-common.S
 *  LOG:
 *      20110127, HSL@RK,add get user regs support and kernel io support.
 */

/* #define DEBUG */
#include <linux/kallsyms.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/slab.h>

/*
 * 20090725,hsl,some debug function start from here
 */
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/mmzone.h>
#include <linux/rtc.h>

#include <asm/sections.h>
#include <asm/stacktrace.h>
#include <asm/io.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
#define FIND_TASK_BY_PID(x) pid_task(find_vpid(x), PIDTYPE_PID)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#define FIND_TASK_BY_PID(x) find_task_by_vpid(x)
#else
#define FIND_TASK_BY_PID(x) find_task_by_pid(x)
#endif

extern int __scu_call_wrap(unsigned long *argv , int argc , int fun );
extern void ret_fast_syscall( void );

void rk28_printk_mem( unsigned int * addr , int words , unsigned int*  show_addr )
{
        unsigned int * reg_base = addr ;
        if( show_addr == NULL ){
                show_addr = addr;
        }
        while( words > 0 ) {
                printk("%p:%08x  %08x  %08x  %08x\n" ,show_addr ,
                        reg_base[0],reg_base[1],reg_base[2],reg_base[3]);
                words -= 4;
                reg_base += 4;
                show_addr += 4;
        }
}

/*
 *  type define :
 *  first for function name , 
 * ',' for argument.
 * \' : char
 * \" : string.
 * other : for int,short,0x for hex.0 for octal.
 * 20090811,for easy use , 0x or 0-9 start means num , other ,means string . 
 * len = 1 string for char.
 * 20100420,add nagative support.
 */
int dbg_get_arg( char ** pp )
{
        char * p = *pp;
        char *t;
        int ret;
        int len ;
        int nag = 0;
        int bnum = 0;
        if( p[0] == '-' ) {
                nag = 1;
                p++;
        }
        if( p[0] == '0' ) {
                if( ( p[1] == 'x' || p[1] == 'X' ) ) {
                        p+= 2;
                        len = sscanf(p,"%x" , &ret );
                        p += len;
                } else {
                        if( p[1] == 0 || p[1] == ',' ) {
                                ret = 0; /* only one '0' , must be zero */
                                p++;
                        } else {
                                p+= 1;
                                len = sscanf(p,"%o" , &ret );
                                p += len;
                        }
                }
                bnum = 1;
        } else if( p[0] >= '1' && p[0] <= '9' ) {
                len = sscanf(p,"%d" , (int*)&ret );
                p += len;
                bnum = 1;
        } else if( p[0] == '\'' ) {
                ret = p[1];
                p++;
        } else {  /* all for string */
                if ( p[0] == '\"' ) {
                        if( p[1] == '\"' ) { /* NULL string */
                                ret = 0;
                                p+=2;     
                        } else {
                                ret = (unsigned long)(p+1);
                                t = strchr( p+1 , '\"' );
                                if( t ) {
                                        *t = 0;
                                        p = t+1 ;
                                } else  {
                                        p++;
                                }
                        }
               }  else {
                        if( *p == ',' )  {/* empty ,as NULL string */
                                ret = 0;
                                p--;
                        } else  if( *p == ' ' || *p == 0 ) /* one char string ,as char.*/
                                ret = *p;
                        else /* as string */
                                ret = (unsigned long)(p);
                        p++;
                }
        }
       t = strchr( p , ',' );
       if( t ) {
            *t = 0;
            *pp = t+1;
       } else { /* p should be the last one */
             while(*p != ' ' && *p != 0 && *p != '\n' )
                        p++;
	*p = 0;
             *pp = p;
       }
       if( nag && bnum )
                ret = 0-ret;
       return ret;
}
int dbg_parse_cmd( char * cdb )
{
        unsigned long reg[6];
        unsigned long fun;
        int             argc = 0 ;
        char    *p = cdb ;
        char    *sym = p;
        int       ret;
        
        //SCU_BUG("CMD=%s\n" , cdb);
        memset( reg , 0 , sizeof reg );
        p = strchr(p , ',');
        if( p ) {
                *p++ = 0;
                while( *p && argc < 6) {
                        reg[argc++] = (unsigned long)dbg_get_arg( &p );
                }
        }
        //debug_print("sym=%s,argc=%d,db=%s\n" , sym ,argc , cdb );
        if( sym[0] == '0' && ( sym[1] == 'x' || sym[1] == 'X' ) ) {
                fun = 0;
                sscanf(sym+2,"%lx" , &fun );
        } else {
                fun = kallsyms_lookup_name(sym);
                //debug_print("after lookup symbols,sym=%s,fun=0x%p\n" , sym ,fun);
        }
        
        /* 20100409,HSL@RK,use printk to print to buffer.
        */
        printk("@CALL@ %s(0x%p),argc=%d\n" , sym , (void*)fun , argc);
        if( fun ) {
                ret = __scu_call_wrap( reg,argc,fun);
        } else {
                ret = -EIO;
        }
        /* 20100722,"@return" for function print end tag.
        */
        printk("@return 0x%x(%d)\n" , ret , ret);
        return ret;
}

#if 0
/**
 * check arg be a string or int or symbol addr .
 * 1: string , 2: kernel sym , 0:other 
 */
int rk28_arg_string( unsigned long arg )
{
        if( arg > (unsigned long)&_end  && arg < 0xc8000000 )
                return 1;
        if( arg >= (unsigned long)&_text && arg < (unsigned long)&_end )
                return 2;
        return 0;
}
#endif

static struct pt_regs * unwind_get_regs(struct task_struct *tsk)
{
	struct stackframe frame;
	register unsigned long current_sp asm ("sp");
       int found = 0;
	//unsigned long  sc;
	
	if (!tsk)
		tsk = current;
		
       printk("tsk = %p,comm=%s,pid=%d,pgd=0x%p\n", tsk , tsk->comm , tsk->pid , tsk->mm->pgd );
	if (tsk == current) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_sp;
		frame.lr = (unsigned long)__builtin_return_address(0);
		frame.pc = (unsigned long)unwind_get_regs;
	} else {
		/* task blocked in __switch_to */
		frame.fp = thread_saved_fp(tsk);
		frame.sp = thread_saved_sp(tsk);
		/*
		 * The function calling __switch_to cannot be a leaf function
		 * so LR is recovered from the stack.
		 */
		frame.lr = 0;
		frame.pc = thread_saved_pc(tsk);
	}

	while (1) {
		int urc;
		//unsigned long where = frame.pc;

		urc = unwind_frame(&frame);
		if (urc < 0)
			break;
		//dump_backtrace_entry(where, frame.pc, frame.sp - 4);
		if( frame.pc == (unsigned long)ret_fast_syscall ){
		        found = 1;
		        break;
		}
	}
	if( !found )
	        return NULL;
	#if 0
	//printk("FRAME:sp=0x%lx,pc=0x%lx,lr=0x%lx,fp=0x%lx\n" , frame.sp,frame.pc,frame.lr,frame.fp );
	//rk28_printk_mem((unsigned int*)(frame.sp-sizeof(struct pt_regs)),2*sizeof(struct pt_regs)/4+8, NULL );
	sc =*( (unsigned long*)(frame.sp-4));
	if( sc >= (unsigned long)&_text && sc < (unsigned long)&_end ){
	    print_symbol("sys call=%s\n",sc );
	}
	#endif
	return (struct pt_regs *)(frame.sp+8); // 8 for reg r4,r5 as fifth and sixth args.
}

struct pt_regs * __scu_get_regs( void )
{
        return unwind_get_regs( NULL );
}

struct pt_regs * scu_get_task_regs(  int pid  )
{
        struct task_struct *t = FIND_TASK_BY_PID( pid );
        return  unwind_get_regs( t );
}

/* 
  *  f: bit0: show kernel , bit1: dump user.
  */
int tstack( int pid , int f)
{
        struct pt_regs * preg;
        struct task_struct *t = FIND_TASK_BY_PID( pid );
        
        if( !t ){
                printk("NO task found for pid %d\n" ,pid );
                t = current ;
        }
        preg = unwind_get_regs( t );
        if( f& 1 ) {
                show_stack( t , NULL );
        }
        if( !preg ) {
                printk("NO user stack found for task %s(%d)\n" , t->comm , t->pid );
        } else {
                //printk("user stack found for task %s(%d):\n" , t->comm , t->pid );
                __show_regs( preg );
                if( f & 2 ) {
                        #define SHOW_STACK_SIZE         (2*1024)
                        int len;
                        unsigned int *ps = (unsigned int*)kmalloc( SHOW_STACK_SIZE , GFP_KERNEL);
                        if( ps ) {
                                len = access_process_vm( t , preg->ARM_sp , ps , SHOW_STACK_SIZE, 0 );
                                rk28_printk_mem( ps , len/4 , (unsigned int* )preg->ARM_sp );
                                kfree( ps );
                        }
                }
        }
        return pid;
}

#if 0
void __rk_force_signal(struct task_struct *tsk, unsigned long addr,
		unsigned int sig, int code )
{
	struct siginfo si;
                printk("%s::send sig %d to task %s\n" , __func__ , sig , tsk->comm );
	si.si_signo = sig;
	si.si_errno = 0;
	si.si_code = code;
	si.si_addr = (void __user *)addr;
	force_sig_info(sig, &si, tsk);
}
int rksignal( int pid , int sig )
{
        struct task_struct *t = FIND_TASK_BY_PID( pid );
        if( !t )
                t = current ;
        __rk_force_signal( t , 0xc0002234 , sig , 0x524b4b52 );
        return 0x201;
}

#include <linux/tty.h>
#include <linux/console.h>
#include <linux/tty_flip.h>  
extern struct console *console_drivers;
// like : echo rk28_uart_input,"\"pwd|ls,echo fdfdf\"">active
int rk28_uart_input( char * str )
{
        char * p = str;
        int     indx;
        struct tty_driver *tty_drv = console_drivers->device( console_drivers , &indx );
        struct tty_struct *tty_s = tty_drv->ttys[indx ];
        int flag = TTY_NORMAL;
        
        printk("indx=%d, tty drv=%p,tty_s=%p,cur console=%s,next console=%p\n", indx , tty_drv , tty_s ,
                console_drivers->name , console_drivers->next );
        //printk("cmd=%s\n" , str );
        if( !p )
                return -EIO;
        
        while( *p ) {
                if( *p == ',' )
                        *p = '\n';
                p++;
        }
        *p++ = '\n';
        *p = 0;

        p = str;
        while( *p ) {
                tty_insert_flip_char(tty_s, *p , flag);
                p++;
        }

        tty_flip_buffer_push( tty_s );
        return 0x28;
}
#endif

#if 0
extern int kernel_getlog( char ** start  , int * offset , int* len );
int rk28_write_file( char *filename )
{
        char *procmtd = "/proc/mtd";
        struct file			*filp = NULL;
        loff_t		                pos;
        long    fd;
        
        ssize_t l=0;
        char    pathname[64];
        char    buf[1024] ;
        mm_segment_t old_fs;
        struct rtc_time tm;
        ktime_t now;        
        char *tmpbuf; 
        int     mtdidx=0;
        int mtdsize = 0x200000; // 2M 
        char *log_buf;
        int     log_len;

        debug_print("%s: filename=%s\n" , __func__ , filename );

        old_fs = get_fs();
        set_fs(KERNEL_DS);        
        fd = sys_open( procmtd , O_RDONLY , 0 );
        if( fd >= 0 ) {
                memset( buf , 0 , 1024 );
                l = sys_read( fd , buf , 1024 );
               // debug_print("/proc/mtd,total byte=%d\n" , l );
                tmpbuf = strstr( buf , filename );
                if( tmpbuf ) {
                        tmpbuf[10] = 0;
                        mtdsize = simple_strtoul(tmpbuf-19, NULL , 16);
                        mtdidx = tmpbuf[-22];
                        //debug_print("size=%s\n,idx=%s\nmtdsize=%x , mtdidx=%d" , tmpbuf-19 , tmpbuf - 22 ,
                        //        mtdsize , mtdidx );
                        mtdsize *= 512;
                        mtdidx -= '0';
                }
                sys_close( fd );
        } else {
                debug_print("open %s failed\n" , procmtd );
        }
        sprintf(pathname , "/dev/block/mtdblock%d" , mtdidx );
        filp = filp_open(pathname, O_WRONLY , 0);
        if( IS_ERR(filp) ) {
                debug_print("open %s failed n" , pathname  );
                goto out_print;
        }
        if (!(filp->f_op->write || filp->f_op->aio_write)) {
                debug_print("can not write file %s \n" , pathname  );
                goto close_file;
        }
        now = ktime_get();
        rtc_time_to_tm( now.tv.sec , &tm );
        printk( "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n"
                          "++++++++++RECORD LOG AT %04d-%02d-%02d %02d:%02d:%02d++++++++++\n"
                          "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n\n\n\n"
                          ,
                tm.tm_year+1900 , tm.tm_mon , tm.tm_mday , tm.tm_hour , tm.tm_min , tm.tm_sec );
        /* 20090924,HSL,FTL only intial first 256M at loader , MUST write 32 sector one time */
        //debug_print("write first pack , pos=%d\n" , pos ); /* pos = 2 ?? */
        kernel_getlog(&log_buf , NULL ,  &log_len );
        log_len = (log_len+(32*512-1)) / (32*512) * (32*512);
        pos = 0;
        l = vfs_write( filp , log_buf  , log_len , &pos );
        do_fsync(filp, 1);
close_file:
        filp_close(filp , NULL);
out_print:  
        set_fs(old_fs);
        debug_print("\nlog len=%d,write=%d\n" ,log_len , l);
        return 0x2b;
}
#endif

#if 1
/*
 * 
 * 20091027,2808SDK,
 * lcd off:byte:81748000,word:39100000.
 * lcd on: byte:90742000,word:43158000  %9.5
 *
 * 20091111,2806 ruiguan,
 * lcd off: byte: 81883000,word: 39209000
 * lcd on: byte: 90843000,word: 43260000

 * 20100516,281x, 133 ddr , lcd on.
 * need 151432000 ns to copy 4096 Kbytes 
 * need 85340000 ns to copy 1024 Kwords 
 LCD OFF.
 * need 144880000 ns to copy 4096 Kbytes 
 * need 83100000 ns to copy 1024 Kwords 
 20100517,281x,200M ddr lcd on.
 * need 90024000 ns to copy 4096 Kbytes 
 * need 47668000 ns to copy 1024 Kwords 
 lcd off
 need 87564000 ns to copy 4096 Kbytes 
 need 46956000 ns to copy 1024 Kwords 
 20100518,266M ddr. lcd on
 need 84460000 ns to copy 4096 Kbytes 
 need 41036000 ns to copy 1024 Kwords 

 need 204568000 ns to copy 4096 Kbytes 
need 116388000 ns to copy 1024 Kwords

 ddr 330M,lcd on.
 need 81944000 ns to copy 4096 Kbytes 
 need 37640000 ns to copy 1024 Kwords
 ahb 180M.ddr 330M.
 need 88908000 ns to copy 4096 Kbytes 
 need 38108000 ns to copy 1024 Kwords

 ahb 200M,DDR 380M,lcd on.
 need 79456000 ns to copy 4096 Kbytes 
 need 34216000 ns to copy 1024 Kwords   %0.14.
 
 need 79432000 ns to copy 4096 Kbytes 
need 34204000 ns to copy 1024 Kwords
LCD OFF.
 need 77260000 ns to copy 4096 Kbytes 
 need 33768000 ns to copy 1024 Kwords

 need 77228000 ns to copy 4096 Kbytes 
need 33764000 ns to copy 1024 Kwords %0.01
 */

/*
on rk29
dyn desktop:
[   63.010000] @CALL@ rk28_memcpy(0xc070148c),argc=0
[   63.100000] need 90342667 ns to copy 4096 Kbytes 
[   63.140000] need 38885333 ns to copy 1024 Kwords 
[   63.160000] need 13030791 ns to memcpy 4096 Kbytes 
[   63.160000] @return 0x2c(44)
# echo rk28_memcpy > cal 
[   65.680000] @CALL@ rk28_memcpy(0xc070148c),argc=0
[   65.740000] need 57542915 ns to copy 4096 Kbytes 
[   65.770000] need 30017666 ns to copy 1024 Kwords 
[   65.810000] need 32929083 ns to memcpy 4096 Kbytes 
[   65.810000] @return 0x2c(44)
# echo rk28_memcpy > cal 
[   67.400000] @CALL@ rk28_memcpy(0xc070148c),argc=0
[   67.460000] need 52001876 ns to copy 4096 Kbytes 
[   67.490000] need 29809209 ns to copy 1024 Kwords 
# [   67.510000] need 20318709 ns to memcpy 4096 Kbytes 
[   67.510000] @return 0x2c(44)
# echo rk28_memcpy > cal 
[   71.800000] @CALL@ rk28_memcpy(0xc070148c),argc=0
[   71.860000] need 57265835 ns to copy 4096 Kbytes 
[   71.900000] need 32522127 ns to copy 1024 Kwords 
[   71.930000] need 27615668 ns to memcpy 4096 Kbytes 
[   71.930000] @return 0x2c(44)


static desktop:

[  171.880000] @CALL@ rk28_memcpy(0xc070148c),argc=0
[  171.910000] need 27547043 ns to copy 4096 Kbytes 
[  171.920000] need 8993501 ns to copy 1024 Kwords 
[  171.930000] need 6791584 ns to memcpy 4096 Kbytes 
[  171.930000] @return 0x2c(44)
# echo rk28_memcpy > cal 
[  174.050000] @CALL@ rk28_memcpy(0xc070148c),argc=0
[  174.080000] need 26437334 ns to copy 4096 Kbytes 
[  174.090000] need 8701667 ns to copy 1024 Kwords 
[  174.100000] need 6639375 ns to memcpy 4096 Kbytes 
[  174.100000] @return 0x2c(44)
# echo rk28_memcpy > cal 
[  176.290000] @CALL@ rk28_memcpy(0xc070148c),argc=0
[  176.320000] need 26692502 ns to copy 4096 Kbytes 
[  176.330000] need 8659126 ns to copy 1024 Kwords 
[  176.340000] need 6702001 ns to memcpy 4096 Kbytes 
[  176.340000] @return 0x2c(44)
# echo rk28_memcpy > cal 
[  177.710000] @CALL@ rk28_memcpy(0xc070148c),argc=0
[  177.740000] need 27578291 ns to copy 4096 Kbytes 
[  177.750000] need 8740042 ns to copy 1024 Kwords 
[  177.760000] need 6727458 ns to memcpy 4096 Kbytes 
[  177.760000] @return 0x2c(44)

*/
int rkmemcpy( void )
{
#define PAGE_ORDER              7
        ktime_t         now0,now1;

        unsigned long pg;
        unsigned long src = 0xc0010000;
        int     i = 8,k=0;
        int     bytes = ((1<<PAGE_ORDER)*PAGE_SIZE);
        
        pg = __get_free_pages(GFP_KERNEL , PAGE_ORDER );
        if( !pg ) {
                printk("alloc %d pages total %dK bytes failed\n" , (1<<PAGE_ORDER) , bytes/(1024));
                return -ENOMEM;
       }
       now0 = ktime_get();
       while( k < i ) {
                char *p = (char*)pg;
                char  *q = (char*)src;
                char  *m = q+bytes;
                while( q < m ) 
                        *p++ = *q++;   
                k++;
       }
       now1 = ktime_get();;
       printk("need %Ld ns to copy %d Kbytes \n" ,
                ktime_to_ns( ktime_sub(  now1 , now0 ) ), bytes * i /1024 );
       now0 = ktime_get();
       k = 0;
       while( k < i ) {
                int *p = (int*)pg;
                int  *q = (int*)src;
                int  *m = q+bytes/sizeof(int);
                while( q < m ) 
                        *p++ = *q++;   
                k++;
       }
       now1 = ktime_get();
       
       printk("need %Ld ns to copy %d Kwords \n" ,
                ktime_to_ns( ktime_sub(  now1 , now0 ) ), bytes * i / sizeof (int) /1024 );

       now0 = ktime_get();
       for( k = 0 ;  k < i ; k++ )
                memcpy((void*)pg,(void*)src , bytes );
       now1 = ktime_get();
       printk("need %Ld ns to memcpy %d Kbytes \n" ,
                ktime_to_ns( ktime_sub(  now1 , now0 ) ), bytes * i / 1024 );

       free_pages( pg , PAGE_ORDER );
       return 0x2c;
}
#endif

int rknow( int loop , int prtk )
{
        ktime_t ktime_now;
        struct timespec ts;
        struct rtc_time tm;

        if( loop > 20 )
            loop = 20;
        do {
        ktime_now = ktime_get();
        getnstimeofday(&ts);
        rtc_time_to_tm(ts.tv_sec, &tm);
        printk("now=%lld ns "
               "(%d-%02d-%02d %02d:%02d:%02d.%09lu UTC)\n",
               ktime_to_ns(ktime_now),
               tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
               tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
        udelay( 31 );
        loop --;
        }while( loop > 0 );
        return 0x2c;
}

#if 0
/* 1,2=0 , 0 = lr */
void rk28_return( void )
{
        printk("return addr:\n 0: 0x%p " "1: 0x%p " "2: 0x%p\n", __builtin_return_address(0) , 
                __builtin_return_address(1) , __builtin_return_address(2));
}
#endif

int dl( int ms )
{
        mdelay( ms );
        printk("delay %d ms\n" , ms );
        return ms;
}

int irq_dl( int ms )
{
        unsigned long flags;
        local_irq_save(flags);
        mdelay( ms );
        local_irq_restore(flags);
        printk("delay %d ms\n" , ms );
        return ms;
}

/*
 * read a phy addr value , len is word.
*/
int dd( unsigned int phy_addr,int len ,unsigned  int value )
{
            int size = len*4;
            unsigned int * ptr;
            int     wr = 0;
            int     map = 0;
            if( !len ){
                    printk("invalid len=%d\n" , len );
                    return 0;
            }
            /* we want to write? write 0?*/
            if( value || len == 1 ) {   
                    size = 4;
                    wr = 1;
            }
            if(phy_addr < PAGE_OFFSET ) {
                    ptr = (unsigned int *)ioremap( phy_addr , size );
                    if( !ptr ) {
                            printk("map addr 0x%x failed\n" , phy_addr );
                            return -ENOMEM;
                    }
                    map = 1;
            } else {
                    ptr = (unsigned int *)phy_addr;
            }
            if( wr ) {
                    *ptr = value ;
                    dmb();
            }
            rk28_printk_mem( ptr , len , (unsigned int*)phy_addr );
            if( map ) {
                    iounmap(ptr);
            }
            return size;
}

