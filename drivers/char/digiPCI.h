/*************************************************************************
 * Defines and structure definitions for PCI BIOS Interface 
 *************************************************************************/
#define	PCIMAX  32		/* maximum number of PCI boards */


#define	PCI_VENDOR_DIGI		0x114F
#define	PCI_DEVICE_EPC		0x0002
#define	PCI_DEVICE_RIGHTSWITCH 0x0003  /* For testing */
#define	PCI_DEVICE_XEM		0x0004
#define	PCI_DEVICE_XR		0x0005
#define	PCI_DEVICE_CX		0x0006
#define	PCI_DEVICE_XRJ		0x0009   /* Jupiter boards with */
#define	PCI_DEVICE_EPCJ		0x000a   /* PLX 9060 chip for PCI  */


/*
 * On the PCI boards, there is no IO space allocated 
 * The I/O registers will be in the first 3 bytes of the   
 * upper 2MB of the 4MB memory space.  The board memory 
 * will be mapped into the low 2MB of the 4MB memory space 
 */

/* Potential location of PCI Bios from E0000 to FFFFF*/
#define PCI_BIOS_SIZE		0x00020000	

/* Size of Memory and I/O for PCI (4MB) */
#define PCI_RAM_SIZE		0x00400000	

/* Size of Memory (2MB) */
#define PCI_MEM_SIZE		0x00200000	

/* Offset of I/0 in Memory (2MB) */
#define PCI_IO_OFFSET 		0x00200000	

#define MEMOUTB(basemem, pnum, setmemval)  *(caddr_t)((basemem) + ( PCI_IO_OFFSET | pnum << 4 | pnum )) = (setmemval)
#define MEMINB(basemem, pnum)  *(caddr_t)((basemem) + (PCI_IO_OFFSET | pnum << 4 | pnum ))   /* for PCI I/O */





