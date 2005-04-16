/* either kmalloc() or bigphysarea() alloced memory - continuous */
void*	bmalloc(unsigned long size);
void	bfree(void* mem, unsigned long size);
