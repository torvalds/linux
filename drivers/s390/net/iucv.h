/*
 *  drivers/s390/net/iucv.h
 *    IUCV base support.
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Corporation
 *    Author(s):Alan Altmark (Alan_Altmark@us.ibm.com) 
 *		Xenia Tkatschow (xenia@us.ibm.com)
 *
 *
 * Functionality:
 * To explore any of the IUCV functions, one must first register
 * their program using iucv_register_program(). Once your program has
 * successfully completed a register, it can exploit the other functions.
 * For furthur reference on all IUCV functionality, refer to the
 * CP Programming Services book, also available on the web
 * thru www.ibm.com/s390/vm/pubs, manual # SC24-5760
 *
 *      Definition of Return Codes                                    
 *      -All positive return codes including zero are reflected back  
 *       from CP except for iucv_register_program. The definition of each 
 *       return code can be found in CP Programming Services book.    
 *       Also available on the web thru www.ibm.com/s390/vm/pubs, manual # SC24-5760          
 *      - Return Code of:         
 *             (-EINVAL) Invalid value       
 *             (-ENOMEM) storage allocation failed              
 *	pgmask defined in iucv_register_program will be set depending on input
 *	paramters. 
 *	
 */

#include <linux/types.h>
#include <asm/debug.h>

/**
 * Debug Facility stuff
 */
#define IUCV_DBF_SETUP_NAME "iucv_setup"
#define IUCV_DBF_SETUP_LEN 32
#define IUCV_DBF_SETUP_PAGES 2
#define IUCV_DBF_SETUP_NR_AREAS 1
#define IUCV_DBF_SETUP_LEVEL 3

#define IUCV_DBF_DATA_NAME "iucv_data"
#define IUCV_DBF_DATA_LEN 128
#define IUCV_DBF_DATA_PAGES 2
#define IUCV_DBF_DATA_NR_AREAS 1
#define IUCV_DBF_DATA_LEVEL 2

#define IUCV_DBF_TRACE_NAME "iucv_trace"
#define IUCV_DBF_TRACE_LEN 16
#define IUCV_DBF_TRACE_PAGES 4
#define IUCV_DBF_TRACE_NR_AREAS 1
#define IUCV_DBF_TRACE_LEVEL 3

#define IUCV_DBF_TEXT(name,level,text) \
	do { \
		debug_text_event(iucv_dbf_##name,level,text); \
	} while (0)

#define IUCV_DBF_HEX(name,level,addr,len) \
	do { \
		debug_event(iucv_dbf_##name,level,(void*)(addr),len); \
	} while (0)

DECLARE_PER_CPU(char[256], iucv_dbf_txt_buf);

#define IUCV_DBF_TEXT_(name,level,text...)				\
	do {								\
		char* iucv_dbf_txt_buf = get_cpu_var(iucv_dbf_txt_buf);	\
		sprintf(iucv_dbf_txt_buf, text);		  	\
		debug_text_event(iucv_dbf_##name,level,iucv_dbf_txt_buf); \
		put_cpu_var(iucv_dbf_txt_buf);				\
	} while (0)

#define IUCV_DBF_SPRINTF(name,level,text...) \
	do { \
		debug_sprintf_event(iucv_dbf_trace, level, ##text ); \
		debug_sprintf_event(iucv_dbf_trace, level, text ); \
	} while (0)

/**
 * some more debug stuff
 */
#define IUCV_HEXDUMP16(importance,header,ptr) \
PRINT_##importance(header "%02x %02x %02x %02x  %02x %02x %02x %02x  " \
		   "%02x %02x %02x %02x  %02x %02x %02x %02x\n", \
		   *(((char*)ptr)),*(((char*)ptr)+1),*(((char*)ptr)+2), \
		   *(((char*)ptr)+3),*(((char*)ptr)+4),*(((char*)ptr)+5), \
		   *(((char*)ptr)+6),*(((char*)ptr)+7),*(((char*)ptr)+8), \
		   *(((char*)ptr)+9),*(((char*)ptr)+10),*(((char*)ptr)+11), \
		   *(((char*)ptr)+12),*(((char*)ptr)+13), \
		   *(((char*)ptr)+14),*(((char*)ptr)+15)); \
PRINT_##importance(header "%02x %02x %02x %02x  %02x %02x %02x %02x  " \
		   "%02x %02x %02x %02x  %02x %02x %02x %02x\n", \
		   *(((char*)ptr)+16),*(((char*)ptr)+17), \
		   *(((char*)ptr)+18),*(((char*)ptr)+19), \
		   *(((char*)ptr)+20),*(((char*)ptr)+21), \
		   *(((char*)ptr)+22),*(((char*)ptr)+23), \
		   *(((char*)ptr)+24),*(((char*)ptr)+25), \
		   *(((char*)ptr)+26),*(((char*)ptr)+27), \
		   *(((char*)ptr)+28),*(((char*)ptr)+29), \
		   *(((char*)ptr)+30),*(((char*)ptr)+31));

static inline void
iucv_hex_dump(unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (i && !(i % 16))
			printk("\n");
		printk("%02x ", *(buf + i));
	}
	printk("\n");
}
/**
 * end of debug stuff
 */

#define uchar  unsigned char
#define ushort unsigned short
#define ulong  unsigned long
#define iucv_handle_t void *

/* flags1:
 * All flags are defined in the field IPFLAGS1 of each function   
 * and can be found in CP Programming Services.                  
 * IPLOCAL  - Indicates the connect can only be satisfied on the 
 *            local system                                       
 * IPPRTY   - Indicates a priority message                       
 * IPQUSCE  - Indicates you do not want to receive messages on a 
 *            path until an iucv_resume is issued                
 * IPRMDATA - Indicates that the message is in the parameter list
 */
#define IPLOCAL   	0x01
#define IPPRTY         	0x20
#define IPQUSCE        	0x40
#define IPRMDATA       	0x80

/* flags1_out:
 * All flags are defined in the output field of IPFLAGS1 for each function
 * and can be found in CP Programming Services.
 * IPNORPY - Specifies this is a one-way message and no reply is expected.
 * IPPRTY   - Indicates a priority message is permitted. Defined in flags1.
 */
#define IPNORPY         0x10

#define Nonpriority_MessagePendingInterruptsFlag         0x80
#define Priority_MessagePendingInterruptsFlag            0x40
#define Nonpriority_MessageCompletionInterruptsFlag      0x20
#define Priority_MessageCompletionInterruptsFlag         0x10
#define IUCVControlInterruptsFlag                        0x08
#define AllInterrupts                                    0xf8
/*
 * Mapping of external interrupt buffers should be used with the corresponding
 * interrupt types.                  
 * Names: iucv_ConnectionPending    ->  connection pending 
 *        iucv_ConnectionComplete   ->  connection complete
 *        iucv_ConnectionSevered    ->  connection severed 
 *        iucv_ConnectionQuiesced   ->  connection quiesced 
 *        iucv_ConnectionResumed    ->  connection resumed 
 *        iucv_MessagePending       ->  message pending    
 *        iucv_MessageComplete      ->  message complete   
 */
typedef struct {
	u16 ippathid;
	uchar ipflags1;
	uchar iptype;
	u16 ipmsglim;
	u16 res1;
	uchar ipvmid[8];
	uchar ipuser[16];
	u32 res3;
	uchar ippollfg;
	uchar res4[3];
} iucv_ConnectionPending;

typedef struct {
	u16 ippathid;
	uchar ipflags1;
	uchar iptype;
	u16 ipmsglim;
	u16 res1;
	uchar res2[8];
	uchar ipuser[16];
	u32 res3;
	uchar ippollfg;
	uchar res4[3];
} iucv_ConnectionComplete;

typedef struct {
	u16 ippathid;
	uchar res1;
	uchar iptype;
	u32 res2;
	uchar res3[8];
	uchar ipuser[16];
	u32 res4;
	uchar ippollfg;
	uchar res5[3];
} iucv_ConnectionSevered;

typedef struct {
	u16 ippathid;
	uchar res1;
	uchar iptype;
	u32 res2;
	uchar res3[8];
	uchar ipuser[16];
	u32 res4;
	uchar ippollfg;
	uchar res5[3];
} iucv_ConnectionQuiesced;

typedef struct {
	u16 ippathid;
	uchar res1;
	uchar iptype;
	u32 res2;
	uchar res3[8];
	uchar ipuser[16];
	u32 res4;
	uchar ippollfg;
	uchar res5[3];
} iucv_ConnectionResumed;

typedef struct {
	u16 ippathid;
	uchar ipflags1;
	uchar iptype;
	u32 ipmsgid;
	u32 iptrgcls;
	union u2 {
		u32 iprmmsg1_u32;
		uchar iprmmsg1[4];
	} ln1msg1;
	union u1 {
		u32 ipbfln1f;
		uchar iprmmsg2[4];
	} ln1msg2;
	u32 res1[3];
	u32 ipbfln2f;
	uchar ippollfg;
	uchar res2[3];
} iucv_MessagePending;

typedef struct {
	u16 ippathid;
	uchar ipflags1;
	uchar iptype;
	u32 ipmsgid;
	u32 ipaudit;
	uchar iprmmsg[8];
	u32 ipsrccls;
	u32 ipmsgtag;
	u32 res;
	u32 ipbfln2f;
	uchar ippollfg;
	uchar res2[3];
} iucv_MessageComplete;

/* 
 * iucv_interrupt_ops_t: Is a vector of functions that handle 
 * IUCV interrupts.                                          
 * Parameter list:                                           
 *         eib - is a pointer to a 40-byte area described    
 *               with one of the structures above.           
 *         pgm_data - this data is strictly for the          
 *                    interrupt handler that is passed by    
 *                    the application. This may be an address 
 *                    or token.                              
*/
typedef struct {
	void (*ConnectionPending) (iucv_ConnectionPending * eib,
				   void *pgm_data);
	void (*ConnectionComplete) (iucv_ConnectionComplete * eib,
				    void *pgm_data);
	void (*ConnectionSevered) (iucv_ConnectionSevered * eib,
				   void *pgm_data);
	void (*ConnectionQuiesced) (iucv_ConnectionQuiesced * eib,
				    void *pgm_data);
	void (*ConnectionResumed) (iucv_ConnectionResumed * eib,
				   void *pgm_data);
	void (*MessagePending) (iucv_MessagePending * eib, void *pgm_data);
	void (*MessageComplete) (iucv_MessageComplete * eib, void *pgm_data);
} iucv_interrupt_ops_t;

/*
 *iucv_array_t : Defines buffer array.                      
 * Inside the array may be 31- bit addresses and 31-bit lengths. 
*/
typedef struct {
	u32 address;
	u32 length;
} iucv_array_t __attribute__ ((aligned (8)));

extern struct bus_type iucv_bus;
extern struct device *iucv_root;

/*   -prototypes-    */
/*                                                                
 * Name: iucv_register_program                                    
 * Purpose: Registers an application with IUCV                    
 * Input: prmname - user identification                           
 *        userid  - machine identification
 *        pgmmask - indicates which bits in the prmname and userid combined will be
 *  		    used to determine who is given control
 *        ops     - address of vector of interrupt handlers       
 *        pgm_data- application data passed to interrupt handlers 
 * Output: NA                                                     
 * Return: address of handler                                     
 *         (0) - Error occurred, registration not completed.
 * NOTE: Exact cause of failure will be recorded in syslog.                        
*/
iucv_handle_t iucv_register_program (uchar pgmname[16],
				     uchar userid[8],
				     uchar pgmmask[24],
				     iucv_interrupt_ops_t * ops,
				     void *pgm_data);

/*                                                
 * Name: iucv_unregister_program                  
 * Purpose: Unregister application with IUCV      
 * Input: address of handler                      
 * Output: NA                                     
 * Return: (0) - Normal return                    
 *         (-EINVAL) - Internal error, wild pointer     
*/
int iucv_unregister_program (iucv_handle_t handle);

/*
 * Name: iucv_accept
 * Purpose: This function is issued after the user receives a Connection Pending external
 *          interrupt and now wishes to complete the IUCV communication path.
 * Input:  pathid - u16 , Path identification number   
 *         msglim_reqstd - u16, The number of outstanding messages requested.
 *         user_data - uchar[16], Data specified by the iucv_connect function.
 *	   flags1 - int, Contains options for this path.
 *           -IPPRTY   - 0x20- Specifies if you want to send priority message.
 *           -IPRMDATA - 0x80, Specifies whether your program can handle a message
 *            	in  the parameter list.
 *           -IPQUSCE  - 0x40, Specifies whether you want to quiesce the path being
 *		established.
 *         handle - iucv_handle_t, Address of handler.
 *         pgm_data - void *, Application data passed to interrupt handlers.
 *         flags1_out - int * Contains information about the path
 *           - IPPRTY - 0x20, Indicates you may send priority messages.
 *         msglim - *u16, Number of outstanding messages.
 * Output: return code from CP IUCV call.
*/

int iucv_accept (u16 pathid,
		 u16 msglim_reqstd,
		 uchar user_data[16],
		 int flags1,
		 iucv_handle_t handle,
		 void *pgm_data, int *flags1_out, u16 * msglim);

/*
 * Name: iucv_connect                                         
 * Purpose: This function establishes an IUCV path. Although the connect may complete
 *	    successfully, you are not able to use the path until you receive an IUCV 
 *          Connection Complete external interrupt.            
 * Input: pathid - u16 *, Path identification number          
 *        msglim_reqstd - u16, Number of outstanding messages requested       
 *        user_data - uchar[16], 16-byte user data                    
 *	  userid - uchar[8], User identification
 *        system_name - uchar[8], 8-byte identifying the system name 
 *	  flags1 - int, Contains options for this path.
 *          -IPPRTY -   0x20, Specifies if you want to send priority message.
 *          -IPRMDATA - 0x80, Specifies whether your program can handle a message
 *            	 in  the parameter list.
 *          -IPQUSCE -  0x40, Specifies whether you want to quiesce the path being	 
 *		established.
 *          -IPLOCAL -  0X01, Allows an application to force the partner to be on 
 *		the local system. If local is specified then target class cannot be
 *		specified.                       
 *        flags1_out - int * Contains information about the path
 *           - IPPRTY - 0x20, Indicates you may send priority messages.
 *        msglim - * u16, Number of outstanding messages
 *        handle - iucv_handle_t, Address of handler                         
 *        pgm_data - void *, Application data passed to interrupt handlers              
 * Output: return code from CP IUCV call
 *         rc - return code from iucv_declare_buffer
 *         -EINVAL - Invalid handle passed by application 
 *         -EINVAL - Pathid address is NULL 
 *         add_pathid_result - Return code from internal function add_pathid         
*/
int
    iucv_connect (u16 * pathid,
		  u16 msglim_reqstd,
		  uchar user_data[16],
		  uchar userid[8],
		  uchar system_name[8],
		  int flags1,
		  int *flags1_out,
		  u16 * msglim, iucv_handle_t handle, void *pgm_data);

/*                                                                     
 * Name: iucv_purge                                                    
 * Purpose: This function cancels a message that you have sent.        
 * Input: pathid - Path identification number.                          
 *        msgid - Specifies the message ID of the message to be purged.
 *        srccls - Specifies the source message class.                  
 * Output: audit - Contains information about asynchronous error       
 *                 that may have affected the normal completion        
 *                 of this message.                                    
 * Return: Return code from CP IUCV call.                           
*/
int iucv_purge (u16 pathid, u32 msgid, u32 srccls, __u32 *audit);
/*
 * Name: iucv_query_maxconn
 * Purpose: This function determines the maximum number of communication paths you
 *	    may establish.
 * Return:  maxconn - ulong, Maximum number of connection the virtual machine may
 *          establish.
*/
ulong iucv_query_maxconn (void);

/*
 * Name: iucv_query_bufsize
 * Purpose: This function determines how large an external interrupt
 *          buffer IUCV requires to store information.
 * Return:  bufsize - ulong, Size of external interrupt buffer.
 */
ulong iucv_query_bufsize (void);

/*                                                                     
 * Name: iucv_quiesce                                                  
 * Purpose: This function temporarily suspends incoming messages on an 
 *          IUCV path. You can later reactivate the path by invoking   
 *          the iucv_resume function.                                  
 * Input: pathid - Path identification number                          
 *        user_data  - 16-bytes of user data                           
 * Output: NA                                                          
 * Return: Return code from CP IUCV call.                           
*/
int iucv_quiesce (u16 pathid, uchar user_data[16]);

/*                                                                     
 * Name: iucv_receive                                                  
 * Purpose: This function receives messages that are being sent to you 
 *          over established paths. Data will be returned in buffer for length of
 *          buflen.
 * Input: 
 *       pathid - Path identification number.                          
 *       buffer - Address of buffer to receive.                        
 *       buflen - Length of buffer to receive.                         
 *       msgid - Specifies the message ID.          
 *       trgcls - Specifies target class.                       
 * Output: 
 *	 flags1_out: int *, Contains information about this path.
 *         IPNORPY - 0x10 Specifies this is a one-way message and no reply is
 *	   expected.      
 *         IPPRTY  - 0x20 Specifies if you want to send priority message.       
 *         IPRMDATA - 0x80 specifies the data is contained in the parameter list
 *       residual_buffer - address of buffer updated by the number
 *                         of bytes you have received.
 *       residual_length -      
 *              Contains one of the following values, if the receive buffer is:
 *               The same length as the message, this field is zero.
 *               Longer than the message, this field contains the number of
 *                bytes remaining in the buffer.
 *               Shorter than the message, this field contains the residual
 *                count (that is, the number of bytes remaining in the
 *                message that does not fit into the buffer. In this
 *                case b2f0_result = 5.
 * Return: Return code from CP IUCV call.                           
 *         (-EINVAL) - buffer address is pointing to NULL                   
*/
int iucv_receive (u16 pathid,
		  u32 msgid,
		  u32 trgcls,
		  void *buffer,
		  ulong buflen,
		  int *flags1_out,
		  ulong * residual_buffer, ulong * residual_length);

 /*                                                                     
  * Name: iucv_receive_array                                            
  * Purpose: This function receives messages that are being sent to you 
  *          over established paths. Data will be returned in first buffer for
  *          length of first buffer.
  * Input: pathid - Path identification number.                          
  *        msgid - specifies the message ID.
  *        trgcls - Specifies target class.
  *        buffer - Address of array of buffers.                         
  *        buflen - Total length of buffers.                             
  * Output:
  *        flags1_out: int *, Contains information about this path.
  *          IPNORPY - 0x10 Specifies this is a one-way message and no reply is
  *          expected.
  *          IPPRTY  - 0x20 Specifies if you want to send priority message.
  *          IPRMDATA - 0x80 specifies the data is contained in the parameter list
  *       residual_buffer - address points to the current list entry IUCV
  *                         is working on.
  *       residual_length -
  *              Contains one of the following values, if the receive buffer is:
  *               The same length as the message, this field is zero.
  *               Longer than the message, this field contains the number of
  *                bytes remaining in the buffer.
  *               Shorter than the message, this field contains the residual
  *                count (that is, the number of bytes remaining in the
  *                message that does not fit into the buffer. In this
  *                case b2f0_result = 5.
  * Return: Return code from CP IUCV call.                           
  *         (-EINVAL) - Buffer address is NULL.       
  */
int iucv_receive_array (u16 pathid,
			u32 msgid,
			u32 trgcls,
			iucv_array_t * buffer,
			ulong buflen,
			int *flags1_out,
			ulong * residual_buffer, ulong * residual_length);

/*                                                                       
 * Name: iucv_reject                                                     
 * Purpose: The reject function refuses a specified message. Between the 
 *          time you are notified of a message and the time that you     
 *          complete the message, the message may be rejected.           
 * Input: pathid - Path identification number.                            
 *        msgid - Specifies the message ID.                   
 *        trgcls - Specifies target class.                                
 * Output: NA                                                            
 * Return: Return code from CP IUCV call.                             
*/
int iucv_reject (u16 pathid, u32 msgid, u32 trgcls);

/*                                                                     
 * Name: iucv_reply                                                    
 * Purpose: This function responds to the two-way messages that you    
 *          receive. You must identify completely the message to       
 *          which you wish to reply. ie, pathid, msgid, and trgcls.    
 * Input: pathid - Path identification number.                          
 *        msgid - Specifies the message ID.                
 *        trgcls - Specifies target class.                              
 *        flags1 - Option for path.
 *          IPPRTY- 0x20, Specifies if you want to send priority message.        
 *        buffer - Address of reply buffer.                             
 *        buflen - Length of reply buffer.                              
 * Output: residual_buffer - Address of buffer updated by the number 
 *                    of bytes you have moved.              
 *         residual_length - Contains one of the following values:
 *		If the answer buffer is the same length as the reply, this field
 *		 contains zero.
 *		If the answer buffer is longer than the reply, this field contains
 *		 the number of bytes remaining in the buffer.  
 *		If the answer buffer is shorter than the reply, this field contains
 *		 a residual count (that is, the number of bytes remianing in the
 *		 reply that does not fit into the buffer. In this
 *               case b2f0_result = 5.
 * Return: Return code from CP IUCV call.                           
 *         (-EINVAL) - Buffer address is NULL.                               
*/
int iucv_reply (u16 pathid,
		u32 msgid,
		u32 trgcls,
		int flags1,
		void *buffer, ulong buflen, ulong * residual_buffer,
		ulong * residual_length);

/*                                                                       
 * Name: iucv_reply_array                                                
 * Purpose: This function responds to the two-way messages that you      
 *          receive. You must identify completely the message to         
 *          which you wish to reply. ie, pathid, msgid, and trgcls.      
 *          The array identifies a list of addresses and lengths of      
 *          discontiguous buffers that contains the reply data.          
 * Input: pathid - Path identification number                            
 *        msgid - Specifies the message ID. 
 *        trgcls - Specifies target class.                                
 *        flags1 - Option for path.
 *          IPPRTY- 0x20, Specifies if you want to send priority message.
 *        buffer - Address of array of reply buffers.                     
 *        buflen - Total length of reply buffers.                         
 * Output: residual_buffer - Address of buffer which IUCV is currently working on.
 *         residual_length - Contains one of the following values:
 *              If the answer buffer is the same length as the reply, this field
 *               contains zero.
 *              If the answer buffer is longer than the reply, this field contains
 *               the number of bytes remaining in the buffer.
 *              If the answer buffer is shorter than the reply, this field contains
 *               a residual count (that is, the number of bytes remianing in the
 *               reply that does not fit into the buffer. In this
 *               case b2f0_result = 5.
 * Return: Return code from CP IUCV call.                             
 *         (-EINVAL) - Buffer address is NULL.              
*/
int iucv_reply_array (u16 pathid,
		      u32 msgid,
		      u32 trgcls,
		      int flags1,
		      iucv_array_t * buffer,
		      ulong buflen, ulong * residual_address,
		      ulong * residual_length);

/*                                                                  
 * Name: iucv_reply_prmmsg                                          
 * Purpose: This function responds to the two-way messages that you 
 *          receive. You must identify completely the message to    
 *          which you wish to reply. ie, pathid, msgid, and trgcls. 
 *          Prmmsg signifies the data is moved into the             
 *          parameter list.                                         
 * Input: pathid - Path identification number.                       
 *        msgid - Specifies the message ID.              
 *        trgcls - Specifies target class.                           
 *        flags1 - Option for path.
 *          IPPRTY- 0x20 Specifies if you want to send priority message.
 *        prmmsg - 8-bytes of data to be placed into the parameter.  
 *                 list.                                            
 * Output: NA                                                       
 * Return: Return code from CP IUCV call.                        
*/
int iucv_reply_prmmsg (u16 pathid,
		       u32 msgid, u32 trgcls, int flags1, uchar prmmsg[8]);

/*                                                                     
 * Name: iucv_resume                                                   
 * Purpose: This function restores communications over a quiesced path 
 * Input: pathid - Path identification number.                          
 *        user_data  - 16-bytes of user data.                           
 * Output: NA                                                          
 * Return: Return code from CP IUCV call.                           
*/
int iucv_resume (u16 pathid, uchar user_data[16]);

/*                                                                   
 * Name: iucv_send                                                   
 * Purpose: This function transmits data to another application.     
 *          Data to be transmitted is in a buffer and this is a      
 *          one-way message and the receiver will not reply to the   
 *          message.                                                 
 * Input: pathid - Path identification number.                        
 *        trgcls - Specifies target class.                            
 *        srccls - Specifies the source message class.                
 *        msgtag - Specifies a tag to be associated with the message. 
 *        flags1 - Option for path.
 *          IPPRTY- 0x20 Specifies if you want to send priority message.
 *        buffer - Address of send buffer.                            
 *        buflen - Length of send buffer.                             
 * Output: msgid - Specifies the message ID.                         
 * Return: Return code from CP IUCV call.                         
 *         (-EINVAL) - Buffer address is NULL.                             
*/
int iucv_send (u16 pathid,
	       u32 * msgid,
	       u32 trgcls,
	       u32 srccls, u32 msgtag, int flags1, void *buffer, ulong buflen);

/*                                                                   
 * Name: iucv_send_array                                             
 * Purpose: This function transmits data to another application.     
 *          The contents of buffer is the address of the array of    
 *          addresses and lengths of discontiguous buffers that hold 
 *          the message text. This is a one-way message and the      
 *          receiver will not reply to the message.                  
 * Input: pathid - Path identification number.                        
 *        trgcls - Specifies target class.                            
 *        srccls - Specifies the source message class.                
 *        msgtag - Specifies a tag to be associated witht the message.
 *        flags1 - Option for path.
 *          IPPRTY- specifies if you want to send priority message. 
 *        buffer - Address of array of send buffers.                  
 *        buflen - Total length of send buffers.                      
 * Output: msgid - Specifies the message ID.                         
 * Return: Return code from CP IUCV call.                         
 *         (-EINVAL) - Buffer address is NULL.                             
*/
int iucv_send_array (u16 pathid,
		     u32 * msgid,
		     u32 trgcls,
		     u32 srccls,
		     u32 msgtag,
		     int flags1, iucv_array_t * buffer, ulong buflen);

/*                                                                     
 * Name: iucv_send_prmmsg                                              
 * Purpose: This function transmits data to another application.       
 *          Prmmsg specifies that the 8-bytes of data are to be moved  
 *          into the parameter list. This is a one-way message and the 
 *          receiver will not reply to the message.                    
 * Input: pathid - Path identification number.                          
 *        trgcls - Specifies target class.                              
 *        srccls - Specifies the source message class.                  
 *        msgtag - Specifies a tag to be associated with the message.   
 *        flags1 - Option for path.
 *          IPPRTY- 0x20 specifies if you want to send priority message.
 *        prmmsg - 8-bytes of data to be placed into parameter list.    
 * Output: msgid - Specifies the message ID.                           
 * Return: Return code from CP IUCV call.                           
*/
int iucv_send_prmmsg (u16 pathid,
		      u32 * msgid,
		      u32 trgcls,
		      u32 srccls, u32 msgtag, int flags1, uchar prmmsg[8]);

/*                                                                
 * Name: iucv_send2way                                            
 * Purpose: This function transmits data to another application.  
 *          Data to be transmitted is in a buffer. The receiver   
 *          of the send is expected to reply to the message and   
 *          a buffer is provided into which IUCV moves the reply  
 *          to this message.                                      
 * Input: pathid - Path identification number.                     
 *        trgcls - Specifies target class.                         
 *        srccls - Specifies the source message class.             
 *        msgtag - Specifies a tag associated with the message.    
 *        flags1 - Option for path.
 *          IPPRTY- 0x20 Specifies if you want to send priority message.
 *        buffer - Address of send buffer.                         
 *        buflen - Length of send buffer.                          
 *        ansbuf - Address of buffer into which IUCV moves the reply of 
 *                 this message.        
 *        anslen - Address of length of buffer.          
 * Output: msgid - Specifies the message ID.                      
 * Return: Return code from CP IUCV call.                      
 *         (-EINVAL) - Buffer or ansbuf address is NULL.    
*/
int iucv_send2way (u16 pathid,
		   u32 * msgid,
		   u32 trgcls,
		   u32 srccls,
		   u32 msgtag,
		   int flags1,
		   void *buffer, ulong buflen, void *ansbuf, ulong anslen);

/*                                                                    
 * Name: iucv_send2way_array                                          
 * Purpose: This function transmits data to another application.      
 *          The contents of buffer is the address of the array of     
 *          addresses and lengths of discontiguous buffers that hold  
 *          the message text. The receiver of the send is expected to 
 *          reply to the message and a buffer is provided into which  
 *          IUCV moves the reply to this message.                     
 * Input: pathid - Path identification number.                         
 *        trgcls - Specifies target class.                             
 *        srccls - Specifies the source message class.                 
 *        msgtag - Specifies a tag to be associated with the message.   
 *        flags1 - Option for path.
 *          IPPRTY- 0x20 Specifies if you want to send priority message.
 *        buffer - Sddress of array of send buffers.                   
 *        buflen - Total length of send buffers.                       
 *        ansbuf - Address of array of buffer into which IUCV moves the reply            
 *                 of this message.                         
 *        anslen - Address of length reply buffers.              
 * Output: msgid - Specifies the message ID.                          
 * Return: Return code from CP IUCV call.                          
 *         (-EINVAL) - Buffer address is NULL.                              
*/
int iucv_send2way_array (u16 pathid,
			 u32 * msgid,
			 u32 trgcls,
			 u32 srccls,
			 u32 msgtag,
			 int flags1,
			 iucv_array_t * buffer,
			 ulong buflen, iucv_array_t * ansbuf, ulong anslen);

/*                                                                     
 * Name: iucv_send2way_prmmsg                                          
 * Purpose: This function transmits data to another application.       
 *          Prmmsg specifies that the 8-bytes of data are to be moved  
 *          into the parameter list. This is a two-way message and the 
 *          receiver of the message is expected to reply. A buffer     
 *          is provided into which IUCV moves the reply to this        
 *          message.                                                   
 * Input: pathid - Rath identification number.                          
 *        trgcls - Specifies target class.                              
 *        srccls - Specifies the source message class.                  
 *        msgtag - Specifies a tag to be associated with the message.   
 *        flags1 - Option for path.
 *          IPPRTY- 0x20 Specifies if you want to send priority message.
 *        prmmsg - 8-bytes of data to be placed in parameter list.      
 *        ansbuf - Address of buffer into which IUCV moves the reply of    
 *                 this message.
 *        anslen - Address of length of buffer.               
 * Output: msgid - Specifies the message ID.                           
 * Return: Return code from CP IUCV call.                           
 *         (-EINVAL) - Buffer address is NULL.         
*/
int iucv_send2way_prmmsg (u16 pathid,
			  u32 * msgid,
			  u32 trgcls,
			  u32 srccls,
			  u32 msgtag,
			  ulong flags1,
			  uchar prmmsg[8], void *ansbuf, ulong anslen);

/*                                                                      
 * Name: iucv_send2way_prmmsg_array                                     
 * Purpose: This function transmits data to another application.        
 *          Prmmsg specifies that the 8-bytes of data are to be moved   
 *          into the parameter list. This is a two-way message and the  
 *          receiver of the message is expected to reply. A buffer      
 *          is provided into which IUCV moves the reply to this         
 *          message. The contents of ansbuf is the address of the       
 *          array of addresses and lengths of discontiguous buffers     
 *          that contain the reply.                                     
 * Input: pathid - Path identification number.                           
 *        trgcls - Specifies target class.                               
 *        srccls - Specifies the source message class.                   
 *        msgtag - Specifies a tag to be associated with the message.    
 *        flags1 - Option for path.
 *          IPPRTY- 0x20 specifies if you want to send priority message.
 *        prmmsg - 8-bytes of data to be placed into the parameter list. 
 *        ansbuf - Address of array of buffer into which IUCV moves the reply
 *                 of this message.  
 *        anslen - Address of length of reply buffers.                
 * Output: msgid - Specifies the message ID.      
 * Return: Return code from CP IUCV call.      
 *         (-EINVAL) - Ansbuf address is NULL.          
*/
int iucv_send2way_prmmsg_array (u16 pathid,
				u32 * msgid,
				u32 trgcls,
				u32 srccls,
				u32 msgtag,
				int flags1,
				uchar prmmsg[8],
				iucv_array_t * ansbuf, ulong anslen);

/*                                                                   
 * Name: iucv_setmask                                                
 * Purpose: This function enables or disables the following IUCV     
 *          external interruptions: Nonpriority and priority message 
 *          interrupts, nonpriority and priority reply interrupts.   
 * Input: SetMaskFlag - options for interrupts
 *           0x80 - Nonpriority_MessagePendingInterruptsFlag         
 *           0x40 - Priority_MessagePendingInterruptsFlag            
 *           0x20 - Nonpriority_MessageCompletionInterruptsFlag      
 *           0x10 - Priority_MessageCompletionInterruptsFlag         
 *           0x08 - IUCVControlInterruptsFlag
 * Output: NA                                                        
 * Return: Return code from CP IUCV call.                         
*/
int iucv_setmask (int SetMaskFlag);

/*                                                  
 * Name: iucv_sever                                 
 * Purpose: This function terminates an IUCV path.  
 * Input: pathid - Path identification number.       
 *        user_data - 16-bytes of user data.         
 * Output: NA       
 * Return: Return code from CP IUCV call.                                
 *         (-EINVAL) - Interal error, wild pointer.       
*/
int iucv_sever (u16 pathid, uchar user_data[16]);
