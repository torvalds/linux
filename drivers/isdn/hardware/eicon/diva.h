/* $Id: diva.h,v 1.1.2.2 2001/02/08 12:25:43 armin Exp $ */

#ifndef __DIVA_XDI_OS_PART_H__
#define __DIVA_XDI_OS_PART_H__


int divasa_xdi_driver_entry(void);
void divasa_xdi_driver_unload(void);
void *diva_driver_add_card(void *pdev, unsigned long CardOrdinal);
void diva_driver_remove_card(void *pdiva);

typedef int (*divas_xdi_copy_to_user_fn_t) (void *os_handle, void __user *dst,
					    const void *src, int length);

typedef int (*divas_xdi_copy_from_user_fn_t) (void *os_handle, void *dst,
					      const void __user *src, int length);

int diva_xdi_read(void *adapter, void *os_handle, void __user *dst,
		  int max_length, divas_xdi_copy_to_user_fn_t cp_fn);

int diva_xdi_write(void *adapter, void *os_handle, const void __user *src,
		   int length, divas_xdi_copy_from_user_fn_t cp_fn);

void *diva_xdi_open_adapter(void *os_handle, const void __user *src,
			    int length,
			    divas_xdi_copy_from_user_fn_t cp_fn);

void diva_xdi_close_adapter(void *adapter, void *os_handle);


#endif
