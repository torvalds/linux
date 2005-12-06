/* $Id: isdn_divert.c,v 1.6.6.3 2001/09/23 22:24:36 kai Exp $
 *
 * DSS1 main diversion supplementary handling for i4l.
 *
 * Copyright 1999       by Werner Cornelius (werner@isdn4linux.de)
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/proc_fs.h>

#include "isdn_divert.h"

/**********************************/
/* structure keeping calling info */
/**********************************/
struct call_struc
  { isdn_ctrl ics; /* delivered setup + driver parameters */
    ulong divert_id; /* Id delivered to user */
    unsigned char akt_state; /* actual state */
    char deflect_dest[35]; /* deflection destination */   
    struct timer_list timer; /* timer control structure */
    char info[90]; /* device info output */ 
    struct call_struc *next; /* pointer to next entry */
    struct call_struc *prev;
  };


/********************************************/
/* structure keeping deflection table entry */
/********************************************/
struct deflect_struc
  { struct deflect_struc *next,*prev; 
    divert_rule rule; /* used rule */
  };


/*****************************************/
/* variables for main diversion services */
/*****************************************/
/* diversion/deflection processes */
static struct call_struc *divert_head = NULL; /* head of remembered entrys */
static ulong next_id = 1; /* next info id */   
static struct deflect_struc *table_head = NULL;
static struct deflect_struc *table_tail = NULL; 
static unsigned char extern_wait_max = 4; /* maximum wait in s for external process */ 

DEFINE_SPINLOCK(divert_lock);

/***************************/
/* timer callback function */
/***************************/
static void deflect_timer_expire(ulong arg)
{
  unsigned long flags;
  struct call_struc *cs = (struct call_struc *) arg;

  spin_lock_irqsave(&divert_lock, flags);
  del_timer(&cs->timer); /* delete active timer */
  spin_unlock_irqrestore(&divert_lock, flags);

  switch(cs->akt_state)
   { case DEFLECT_PROCEED:
       cs->ics.command = ISDN_CMD_HANGUP; /* cancel action */
       divert_if.ll_cmd(&cs->ics);                   	  
       spin_lock_irqsave(&divert_lock, flags);
       cs->akt_state = DEFLECT_AUTODEL; /* delete after timeout */
       cs->timer.expires = jiffies + (HZ * AUTODEL_TIME);
       add_timer(&cs->timer);
       spin_unlock_irqrestore(&divert_lock, flags);
       break;

     case DEFLECT_ALERT:
       cs->ics.command = ISDN_CMD_REDIR; /* protocol */
       strcpy(cs->ics.parm.setup.phone,cs->deflect_dest);
       strcpy(cs->ics.parm.setup.eazmsn,"Testtext delayed");
       divert_if.ll_cmd(&cs->ics);
       spin_lock_irqsave(&divert_lock, flags);
       cs->akt_state = DEFLECT_AUTODEL; /* delete after timeout */
       cs->timer.expires = jiffies + (HZ * AUTODEL_TIME);
       add_timer(&cs->timer);
       spin_unlock_irqrestore(&divert_lock, flags);
       break;

     case DEFLECT_AUTODEL:
     default:
       spin_lock_irqsave(&divert_lock, flags);
       if (cs->prev) 
         cs->prev->next = cs->next; /* forward link */
        else
         divert_head = cs->next;
       if (cs->next)
         cs->next->prev = cs->prev; /* back link */           
       spin_unlock_irqrestore(&divert_lock, flags);
       kfree(cs);
       return;

   } /* switch */
} /* deflect_timer_func */


/*****************************************/
/* handle call forwarding de/activations */
/* 0 = deact, 1 = act, 2 = interrogate   */
/*****************************************/
int cf_command(int drvid, int mode, 
               u_char proc, char *msn, 
               u_char service, char *fwd_nr, ulong *procid)
{ unsigned long flags;
  int retval,msnlen;
  int fwd_len;
  char *p,*ielenp,tmp[60];
  struct call_struc *cs;

  if (strchr(msn,'.')) return(-EINVAL); /* subaddress not allowed in msn */
  if ((proc & 0x7F) > 2) return(-EINVAL);
  proc &= 3;
  p = tmp;
  *p++ = 0x30; /* enumeration */
  ielenp = p++; /* remember total length position */
  *p++ = 0xa; /* proc tag */
  *p++ = 1;   /* length */
  *p++ = proc & 0x7F; /* procedure to de/activate/interrogate */
  *p++ = 0xa; /* service tag */
  *p++ = 1;   /* length */
  *p++ = service; /* service to handle */

  if (mode == 1) 
   { if (!*fwd_nr) return(-EINVAL); /* destination missing */
     if (strchr(fwd_nr,'.')) return(-EINVAL); /* subaddress not allowed */
     fwd_len = strlen(fwd_nr);
     *p++ = 0x30; /* number enumeration */
     *p++ = fwd_len + 2; /* complete forward to len */ 
     *p++ = 0x80; /* fwd to nr */
     *p++ = fwd_len; /* length of number */
     strcpy(p,fwd_nr); /* copy number */
     p += fwd_len; /* pointer beyond fwd */
   } /* activate */

  msnlen = strlen(msn);
  *p++ = 0x80; /* msn number */
  if (msnlen > 1)
   { *p++ = msnlen; /* length */
     strcpy(p,msn);
     p += msnlen;
   }
  else *p++ = 0;

  *ielenp = p - ielenp - 1; /* set total IE length */ 

  /* allocate mem for information struct */  
  if (!(cs = (struct call_struc *) kmalloc(sizeof(struct call_struc), GFP_ATOMIC))) 
             return(-ENOMEM); /* no memory */
  init_timer(&cs->timer);
  cs->info[0] = '\0';
  cs->timer.function = deflect_timer_expire;
  cs->timer.data = (ulong) cs; /* pointer to own structure */
  cs->ics.driver = drvid;
  cs->ics.command = ISDN_CMD_PROT_IO; /* protocol specific io */
  cs->ics.arg = DSS1_CMD_INVOKE; /* invoke supplementary service */
  cs->ics.parm.dss1_io.proc = (mode == 1) ? 7: (mode == 2) ? 11:8; /* operation */ 
  cs->ics.parm.dss1_io.timeout = 4000; /* from ETS 300 207-1 */
  cs->ics.parm.dss1_io.datalen = p - tmp; /* total len */
  cs->ics.parm.dss1_io.data = tmp; /* start of buffer */
  
  spin_lock_irqsave(&divert_lock, flags);
  cs->ics.parm.dss1_io.ll_id = next_id++; /* id for callback */
  spin_unlock_irqrestore(&divert_lock, flags);
  *procid = cs->ics.parm.dss1_io.ll_id;  

  sprintf(cs->info,"%d 0x%lx %s%s 0 %s %02x %d%s%s\n",
	  (!mode ) ? DIVERT_DEACTIVATE : (mode == 1) ? DIVERT_ACTIVATE : DIVERT_REPORT,
          cs->ics.parm.dss1_io.ll_id,
          (mode != 2) ? "" : "0 ",
          divert_if.drv_to_name(cs->ics.driver),
          msn,
          service & 0xFF,
          proc,
          (mode != 1) ? "" : " 0 ",
          (mode != 1) ? "" : fwd_nr);
 
  retval = divert_if.ll_cmd(&cs->ics); /* excute command */

  if (!retval)
   { cs->prev = NULL;
     spin_lock_irqsave(&divert_lock, flags);
     cs->next = divert_head;
     divert_head = cs; 
     spin_unlock_irqrestore(&divert_lock, flags);
   }
  else
   kfree(cs);
  return(retval); 
} /* cf_command */


/****************************************/
/* handle a external deflection command */
/****************************************/
int deflect_extern_action(u_char cmd, ulong callid, char *to_nr)
{ struct call_struc *cs;
  isdn_ctrl ic;
  unsigned long flags;
  int i;

  if ((cmd & 0x7F) > 2) return(-EINVAL); /* invalid command */
  cs = divert_head; /* start of parameter list */
  while (cs)
   { if (cs->divert_id == callid) break; /* found */
     cs = cs->next;  
   } /* search entry */
  if (!cs) return(-EINVAL); /* invalid callid */

  ic.driver = cs->ics.driver;
  ic.arg = cs->ics.arg;
  i = -EINVAL;
  if (cs->akt_state == DEFLECT_AUTODEL) return(i); /* no valid call */
  switch (cmd & 0x7F)
   { case 0: /* hangup */
       del_timer(&cs->timer); 
       ic.command = ISDN_CMD_HANGUP;
       i = divert_if.ll_cmd(&ic);
       spin_lock_irqsave(&divert_lock, flags);
       cs->akt_state = DEFLECT_AUTODEL; /* delete after timeout */
       cs->timer.expires = jiffies + (HZ * AUTODEL_TIME);
       add_timer(&cs->timer);
       spin_unlock_irqrestore(&divert_lock, flags);
     break;      

     case 1: /* alert */
       if (cs->akt_state == DEFLECT_ALERT) return(0);
       cmd &= 0x7F; /* never wait */
       del_timer(&cs->timer); 
       ic.command = ISDN_CMD_ALERT;
       if ((i = divert_if.ll_cmd(&ic)))
	{
          spin_lock_irqsave(&divert_lock, flags);
          cs->akt_state = DEFLECT_AUTODEL; /* delete after timeout */
          cs->timer.expires = jiffies + (HZ * AUTODEL_TIME);
          add_timer(&cs->timer);
          spin_unlock_irqrestore(&divert_lock, flags);
        }
       else
          cs->akt_state = DEFLECT_ALERT; 
     break;      

     case 2: /* redir */
       del_timer(&cs->timer); 
       strcpy(cs->ics.parm.setup.phone, to_nr);
       strcpy(cs->ics.parm.setup.eazmsn, "Testtext manual");
       ic.command = ISDN_CMD_REDIR;
       if ((i = divert_if.ll_cmd(&ic)))
	{
          spin_lock_irqsave(&divert_lock, flags);
          cs->akt_state = DEFLECT_AUTODEL; /* delete after timeout */
          cs->timer.expires = jiffies + (HZ * AUTODEL_TIME);
          add_timer(&cs->timer);
          spin_unlock_irqrestore(&divert_lock, flags);
        }
       else
          cs->akt_state = DEFLECT_ALERT; 
     break;      

   } /* switch */
  return(i);
} /* deflect_extern_action */

/********************************/
/* insert a new rule before idx */
/********************************/
int insertrule(int idx, divert_rule *newrule)
{ struct deflect_struc *ds,*ds1=NULL;
  unsigned long flags;

  if (!(ds = (struct deflect_struc *) kmalloc(sizeof(struct deflect_struc), 
                                              GFP_KERNEL))) 
    return(-ENOMEM); /* no memory */

  ds->rule = *newrule; /* set rule */

  spin_lock_irqsave(&divert_lock, flags);

  if (idx >= 0)
   { ds1 = table_head;
     while ((ds1) && (idx > 0))
      { idx--;
        ds1 = ds1->next;
      } 
     if (!ds1) idx = -1; 
   }

  if (idx < 0)
   { ds->prev = table_tail; /* previous entry */
     ds->next = NULL; /* end of chain */
     if (ds->prev) 
       ds->prev->next = ds; /* last forward */
      else
        table_head = ds; /* is first entry */
     table_tail = ds; /* end of queue */
   }
  else
    { ds->next = ds1; /* next entry */
      ds->prev = ds1->prev; /* prev entry */
      ds1->prev = ds; /* backward chain old element */
      if (!ds->prev)
        table_head = ds; /* first element */
   }

  spin_unlock_irqrestore(&divert_lock, flags);
  return(0);
} /* insertrule */

/***********************************/
/* delete the rule at position idx */
/***********************************/
int deleterule(int idx)
{ struct deflect_struc *ds,*ds1;
  unsigned long flags;
  
  if (idx < 0) 
   { spin_lock_irqsave(&divert_lock, flags);
     ds = table_head;
     table_head = NULL;
     table_tail = NULL;
     spin_unlock_irqrestore(&divert_lock, flags);
     while (ds)
      { ds1 = ds; 
        ds = ds->next;
        kfree(ds1);
      } 
     return(0); 
   }

  spin_lock_irqsave(&divert_lock, flags);
  ds = table_head;

  while ((ds) && (idx > 0))
   { idx--; 
     ds = ds->next;  
   }

  if (!ds) 
   {
     spin_unlock_irqrestore(&divert_lock, flags);
     return(-EINVAL);
   }  

  if (ds->next) 
    ds->next->prev = ds->prev; /* backward chain */
   else
     table_tail = ds->prev; /* end of chain */

  if (ds->prev)
    ds->prev->next = ds->next; /* forward chain */
   else
     table_head = ds->next; /* start of chain */      
  
  spin_unlock_irqrestore(&divert_lock, flags);
  kfree(ds);
  return(0);
} /* deleterule */

/*******************************************/
/* get a pointer to a specific rule number */
/*******************************************/
divert_rule *getruleptr(int idx)
{ struct deflect_struc *ds = table_head;
  
  if (idx < 0) return(NULL);
  while ((ds) && (idx >= 0))
   { if (!(idx--)) 
      { return(&ds->rule);
        break;
      }
     ds = ds->next;  
   }
  return(NULL);
} /* getruleptr */

/*************************************************/
/* called from common module on an incoming call */
/*************************************************/
static int isdn_divert_icall(isdn_ctrl *ic)
{ int retval = 0;
  unsigned long flags;
  struct call_struc *cs = NULL; 
  struct deflect_struc *dv;
  char *p,*p1;
  u_char accept;

  /* first check the internal deflection table */
  for (dv = table_head; dv ; dv = dv->next )
   { /* scan table */
     if (((dv->rule.callopt == 1) && (ic->command == ISDN_STAT_ICALLW)) ||
         ((dv->rule.callopt == 2) && (ic->command == ISDN_STAT_ICALL)))
       continue; /* call option check */  
     if (!(dv->rule.drvid & (1L << ic->driver))) 
       continue; /* driver not matching */ 
     if ((dv->rule.si1) && (dv->rule.si1 != ic->parm.setup.si1)) 
       continue; /* si1 not matching */
     if ((dv->rule.si2) && (dv->rule.si2 != ic->parm.setup.si2)) 
       continue; /* si2 not matching */

     p = dv->rule.my_msn;
     p1 = ic->parm.setup.eazmsn;
     accept = 0;
     while (*p)
      { /* complete compare */
        if (*p == '-')
	  { accept = 1; /* call accepted */
            break;
          }
        if (*p++ != *p1++) 
          break; /* not accepted */
        if ((!*p) && (!*p1))
          accept = 1;
      } /* complete compare */
     if (!accept) continue; /* not accepted */
 
     if ((strcmp(dv->rule.caller,"0")) || (ic->parm.setup.phone[0]))
      { p = dv->rule.caller;
        p1 = ic->parm.setup.phone;
        accept = 0;
        while (*p)
	 { /* complete compare */
           if (*p == '-')
	    { accept = 1; /* call accepted */
              break;
            }
           if (*p++ != *p1++) 
             break; /* not accepted */
           if ((!*p) && (!*p1))
             accept = 1;
         } /* complete compare */
        if (!accept) continue; /* not accepted */
      }  

     switch (dv->rule.action)
       { case DEFLECT_IGNORE:
           return(0);
           break;

         case DEFLECT_ALERT:
         case DEFLECT_PROCEED:
         case DEFLECT_REPORT:
         case DEFLECT_REJECT:
           if (dv->rule.action == DEFLECT_PROCEED)
	    if ((!if_used) || ((!extern_wait_max) && (!dv->rule.waittime))) 
              return(0); /* no external deflection needed */  
           if (!(cs = (struct call_struc *) kmalloc(sizeof(struct call_struc), GFP_ATOMIC))) 
             return(0); /* no memory */
           init_timer(&cs->timer);
           cs->info[0] = '\0';
           cs->timer.function = deflect_timer_expire;
           cs->timer.data = (ulong) cs; /* pointer to own structure */
           
           cs->ics = *ic; /* copy incoming data */
           if (!cs->ics.parm.setup.phone[0]) strcpy(cs->ics.parm.setup.phone,"0");
           if (!cs->ics.parm.setup.eazmsn[0]) strcpy(cs->ics.parm.setup.eazmsn,"0");
	   cs->ics.parm.setup.screen = dv->rule.screen;  
           if (dv->rule.waittime) 
             cs->timer.expires = jiffies + (HZ * dv->rule.waittime);
           else
            if (dv->rule.action == DEFLECT_PROCEED)
              cs->timer.expires = jiffies + (HZ * extern_wait_max); 
            else  
              cs->timer.expires = 0;
           cs->akt_state = dv->rule.action;                
           spin_lock_irqsave(&divert_lock, flags);
           cs->divert_id = next_id++; /* new sequence number */
           spin_unlock_irqrestore(&divert_lock, flags);
           cs->prev = NULL;
           if (cs->akt_state == DEFLECT_ALERT)
             { strcpy(cs->deflect_dest,dv->rule.to_nr);
               if (!cs->timer.expires)
		 { strcpy(ic->parm.setup.eazmsn,"Testtext direct");
                   ic->parm.setup.screen = dv->rule.screen;
                   strcpy(ic->parm.setup.phone,dv->rule.to_nr);
                   cs->akt_state = DEFLECT_AUTODEL; /* delete after timeout */
                   cs->timer.expires = jiffies + (HZ * AUTODEL_TIME);
                   retval = 5; 
                 }
               else
                 retval = 1; /* alerting */                 
             }
           else
             { cs->deflect_dest[0] = '\0';
	       retval = 4; /* only proceed */
             }  
           sprintf(cs->info,"%d 0x%lx %s %s %s %s 0x%x 0x%x %d %d %s\n",
                   cs->akt_state,
                   cs->divert_id,
                   divert_if.drv_to_name(cs->ics.driver),
                   (ic->command == ISDN_STAT_ICALLW) ? "1":"0", 
                   cs->ics.parm.setup.phone, 
                   cs->ics.parm.setup.eazmsn,
                   cs->ics.parm.setup.si1,
                   cs->ics.parm.setup.si2,
                   cs->ics.parm.setup.screen,
                   dv->rule.waittime,
                   cs->deflect_dest);
           if ((dv->rule.action == DEFLECT_REPORT) ||
               (dv->rule.action == DEFLECT_REJECT))
	    { put_info_buffer(cs->info);
	      kfree(cs); /* remove */
              return((dv->rule.action == DEFLECT_REPORT) ? 0:2); /* nothing to do */ 
            }              
           break;
  
         default:
           return(0); /* ignore call */
           break;
       } /* switch action */    
     break; 
   } /* scan_table */

  if (cs) 
   { cs->prev = NULL;
     spin_lock_irqsave(&divert_lock, flags);
     cs->next = divert_head;
     divert_head = cs; 
     if (cs->timer.expires) add_timer(&cs->timer);
     spin_unlock_irqrestore(&divert_lock, flags);

     put_info_buffer(cs->info); 
     return(retval);
   }
  else
     return(0);
} /* isdn_divert_icall */


void deleteprocs(void)
{ struct call_struc *cs, *cs1; 
  unsigned long flags;

  spin_lock_irqsave(&divert_lock, flags);
  cs = divert_head;
  divert_head = NULL;
  while (cs)
   { del_timer(&cs->timer);
     cs1 = cs;
     cs = cs->next;
     kfree(cs1);
   } 
  spin_unlock_irqrestore(&divert_lock, flags);
} /* deleteprocs */

/****************************************************/
/* put a address including address type into buffer */
/****************************************************/
static int put_address(char *st, u_char *p, int len)
{ u_char retval = 0;
  u_char adr_typ = 0; /* network standard */

  if (len < 2) return(retval);
  if (*p == 0xA1)
   { retval = *(++p) + 2; /* total length */
     if (retval > len) return(0); /* too short */
     len = retval - 2; /* remaining length */
     if (len < 3) return(0);
     if ((*(++p) != 0x0A) || (*(++p) != 1)) return(0);
     adr_typ = *(++p);
     len -= 3;
     p++;
     if (len < 2) return(0);
     if (*p++ != 0x12) return(0);
     if (*p > len) return(0); /* check number length */
     len = *p++;
   }   
  else
   if (*p == 0x80)
    { retval = *(++p) + 2; /* total length */
      if (retval > len) return(0);
      len = retval - 2;
      p++;
    }
   else  
    return(0); /* invalid address information */

  sprintf(st,"%d ",adr_typ);
  st += strlen(st);
  if (!len) 
    *st++ = '-';
  else
   while (len--)
     *st++ = *p++;
  *st = '\0';
  return(retval);
} /* put_address */

/*************************************/
/* report a succesfull interrogation */
/*************************************/
static int interrogate_success(isdn_ctrl *ic, struct call_struc *cs)
{ char *src = ic->parm.dss1_io.data;
  int restlen = ic->parm.dss1_io.datalen;
  int cnt = 1;
  u_char n,n1;
  char st[90], *p, *stp;

  if (restlen < 2) return(-100); /* frame too short */
  if (*src++ != 0x30) return(-101);
  if ((n = *src++) > 0x81) return(-102); /* invalid length field */
  restlen -= 2; /* remaining bytes */
  if (n == 0x80)
   { if (restlen < 2) return(-103);
     if ((*(src+restlen-1)) || (*(src+restlen-2))) return(-104);
     restlen -= 2;
   }
  else
   if ( n == 0x81)
    { n = *src++;
      restlen--;
      if (n > restlen) return(-105);
      restlen = n;
    }
   else
    if (n > restlen) return(-106);
     else 
      restlen = n; /* standard format */   
  if (restlen < 3) return(-107); /* no procedure */
  if ((*src++ != 2) || (*src++ != 1) || (*src++ != 0x0B)) return(-108);
  restlen -= 3; 
  if (restlen < 2) return(-109); /* list missing */
  if (*src == 0x31)
   { src++; 
     if ((n = *src++) > 0x81) return(-110); /* invalid length field */
     restlen -= 2; /* remaining bytes */
     if (n == 0x80)
      { if (restlen < 2) return(-111);
        if ((*(src+restlen-1)) || (*(src+restlen-2))) return(-112);
        restlen -= 2;
      }
     else
      if ( n == 0x81)
       { n = *src++;
         restlen--;
         if (n > restlen) return(-113);
         restlen = n;
       }
      else
       if (n > restlen) return(-114);
        else 
         restlen = n; /* standard format */   
   } /* result list header */ 

  while (restlen >= 2)
   { stp = st;
     sprintf(stp,"%d 0x%lx %d %s ",DIVERT_REPORT, ic->parm.dss1_io.ll_id,
                 cnt++,divert_if.drv_to_name(ic->driver));
     stp += strlen(stp);
     if (*src++ != 0x30) return(-115); /* invalid enum */
     n = *src++;
     restlen -= 2;
     if (n > restlen) return(-116); /* enum length wrong */
     restlen -= n;
     p = src; /* one entry */
     src += n;
     if (!(n1 = put_address(stp,p,n & 0xFF))) continue;
     stp += strlen(stp);
     p += n1;
     n -= n1;
     if (n < 6) continue; /* no service and proc */
     if ((*p++ != 0x0A) || (*p++ != 1)) continue;
     sprintf(stp," 0x%02x ",(*p++) & 0xFF);
     stp += strlen(stp);
     if ((*p++ != 0x0A) || (*p++ != 1)) continue;
     sprintf(stp,"%d ",(*p++) & 0xFF);
     stp += strlen(stp);
     n -= 6;
     if (n > 2)
      { if (*p++ != 0x30) continue;
        if (*p > (n-2)) continue;
        n = *p++;
        if (!(n1 = put_address(stp,p,n & 0xFF))) continue;
        stp += strlen(stp);
      }
     sprintf(stp,"\n");
     put_info_buffer(st);
   } /* while restlen */
  if (restlen) return(-117);
  return(0);   
} /* interrogate_success */

/*********************************************/
/* callback for protocol specific extensions */
/*********************************************/
static int prot_stat_callback(isdn_ctrl *ic)
{ struct call_struc *cs, *cs1;
  int i;
  unsigned long flags;

  cs = divert_head; /* start of list */
  cs1 = NULL;
  while (cs)
   { if (ic->driver == cs->ics.driver) 
      { switch (cs->ics.arg)
	 { case DSS1_CMD_INVOKE:
             if ((cs->ics.parm.dss1_io.ll_id == ic->parm.dss1_io.ll_id) &&
                 (cs->ics.parm.dss1_io.hl_id == ic->parm.dss1_io.hl_id))
	      { switch (ic->arg)
		{  case DSS1_STAT_INVOKE_ERR:
                     sprintf(cs->info,"128 0x%lx 0x%x\n", 
                             ic->parm.dss1_io.ll_id,
                             ic->parm.dss1_io.timeout);
                     put_info_buffer(cs->info);
                   break;
                   
                   case DSS1_STAT_INVOKE_RES:
                     switch (cs->ics.parm.dss1_io.proc)
		      {  case  7:
                         case  8:
                            put_info_buffer(cs->info); 
                           break;
                       
                         case  11:
                           i = interrogate_success(ic,cs);
                           if (i)
                             sprintf(cs->info,"%d 0x%lx %d\n",DIVERT_REPORT, 
                                     ic->parm.dss1_io.ll_id,i);
                           put_info_buffer(cs->info); 
                           break;
                       
		         default: 
                           printk(KERN_WARNING "dss1_divert: unknown proc %d\n",cs->ics.parm.dss1_io.proc);
                           break;
                      } 


                   break;
 
		   default:
                     printk(KERN_WARNING "dss1_divert unknown invoke answer %lx\n",ic->arg);
                   break;  
                 } 
                cs1 = cs; /* remember structure */
                cs = NULL; 
                continue; /* abort search */
              } /* id found */ 
           break;
   
	   case DSS1_CMD_INVOKE_ABORT:
             printk(KERN_WARNING "dss1_divert unhandled invoke abort\n"); 
           break;   
         
	   default:
             printk(KERN_WARNING "dss1_divert unknown cmd 0x%lx\n",cs->ics.arg); 
           break; 
         } /* switch ics.arg */ 
        cs = cs->next; 
      } /* driver ok */
   }  
   
  if (!cs1) 
   { printk(KERN_WARNING "dss1_divert unhandled process\n");
     return(0);
   }  

  if (cs1->ics.driver == -1)
   {
     spin_lock_irqsave(&divert_lock, flags);
     del_timer(&cs1->timer);
     if (cs1->prev) 
       cs1->prev->next = cs1->next; /* forward link */
     else
       divert_head = cs1->next;
     if (cs1->next)
       cs1->next->prev = cs1->prev; /* back link */           
     spin_unlock_irqrestore(&divert_lock, flags);
     kfree(cs1);
   } 

  return(0);
} /* prot_stat_callback */


/***************************/
/* status callback from HL */
/***************************/
static int isdn_divert_stat_callback(isdn_ctrl *ic)
{ struct call_struc *cs, *cs1;
  unsigned long flags;
  int retval;

  retval = -1;
  cs = divert_head; /* start of list */
     while (cs)
      { if ((ic->driver == cs->ics.driver) && (ic->arg == cs->ics.arg))
         { switch (ic->command)
	    { case ISDN_STAT_DHUP:
                sprintf(cs->info,"129 0x%lx\n",cs->divert_id);
                del_timer(&cs->timer);
                cs->ics.driver = -1;
                break;

	      case ISDN_STAT_CAUSE:
                sprintf(cs->info,"130 0x%lx %s\n",cs->divert_id,ic->parm.num);
                break;

	      case ISDN_STAT_REDIR:
                sprintf(cs->info,"131 0x%lx\n",cs->divert_id);
                del_timer(&cs->timer);
                cs->ics.driver = -1;
                break; 

	      default:
                sprintf(cs->info,"999 0x%lx 0x%x\n",cs->divert_id,(int)(ic->command));
                break; 
            }
          put_info_buffer(cs->info);
          retval = 0; 
         }
        cs1 = cs; 
        cs = cs->next;
        if (cs1->ics.driver == -1)
          { 
            spin_lock_irqsave(&divert_lock, flags);
            if (cs1->prev) 
              cs1->prev->next = cs1->next; /* forward link */
            else
              divert_head = cs1->next;
            if (cs1->next)
              cs1->next->prev = cs1->prev; /* back link */           
            spin_unlock_irqrestore(&divert_lock, flags);
            kfree(cs1);
          } 
      }  
  return(retval); /* not found */
} /* isdn_divert_stat_callback */ 


/********************/
/* callback from ll */
/********************/ 
int ll_callback(isdn_ctrl *ic)
{
  switch (ic->command)
   { case ISDN_STAT_ICALL:
     case ISDN_STAT_ICALLW:
       return(isdn_divert_icall(ic));
     break;

     case ISDN_STAT_PROT:
       if ((ic->arg & 0xFF) == ISDN_PTYPE_EURO)
	{ if (ic->arg != DSS1_STAT_INVOKE_BRD)
            return(prot_stat_callback(ic));
          else
            return(0); /* DSS1 invoke broadcast */
        }
       else
         return(-1); /* protocol not euro */    

     default:
       return(isdn_divert_stat_callback(ic));
   }
} /* ll_callback */

