/* stuff needed for libgcc on win32.  */

#ifdef L_chkstk

	.global __chkstk
	.global	__alloca
__chkstk:
__alloca:
	pushl  %ecx		/* save temp */
	movl   %esp,%ecx	/* get sp */
	addl   $0x8,%ecx	/* and point to return addr */

probe: 	cmpl   $0x1000,%eax	/* > 4k ?*/
	jb    done		

	subl   $0x1000,%ecx  		/* yes, move pointer down 4k*/
	orl    $0x0,(%ecx)   		/* probe there */
	subl   $0x1000,%eax  	 	/* decrement count */
	jmp    probe           	 	/* and do it again */

done: 	subl   %eax,%ecx	   
	orl    $0x0,(%ecx)	/* less that 4k, just peek here */

	movl   %esp,%eax
	movl   %ecx,%esp	/* decrement stack */

	movl   (%eax),%ecx	/* recover saved temp */
	movl   4(%eax),%eax	/* get return address */
	jmp    *%eax	


#endif
