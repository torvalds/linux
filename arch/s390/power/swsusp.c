/*
 * Support for suspend and resume on s390
 *
 * Copyright IBM Corp. 2009
 *
 * Author(s): Hans-Joachim Picht <hans@linux.vnet.ibm.com>
 *
 */


/*
 * save CPU registers before creating a hibernation image and before
 * restoring the memory state from it
 */
void save_processor_state(void)
{
	/* implentation contained in the
	 * swsusp_arch_suspend function
	 */
}

/*
 * restore the contents of CPU registers
 */
void restore_processor_state(void)
{
	/* implentation contained in the
	 * swsusp_arch_resume function
	 */
}
