/*
 *  HID driver for some microsoft "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2007 Paul Walmsley
 *  Copyright (c) 2008 Jiri Slaby
 */

/*
 * MICROSOFT SOFTWARE END-USER LICENSE AGREEMENT
 * These license terms are an agreement between you and
 * · the computer manufacturer that distributes the software with the computer, or
 * · the software installer that distributes the software with the computer.
 * Please read them. They apply to the software named above, which includes the media on which you received it, if any. Printed-paper license terms, which may come with the software take the place of any on-screen license terms. These terms also apply to any Microsoft
 * · updates,
 * · supplements,
 * · Internet-based services, and
 * · support services
 * for this software, unless other terms accompany those items. If so, those other terms apply.
 * If you obtain updates or supplements directly from Microsoft, Microsoft, and not the manufacturer or installer, licenses those to you.
 * By using the software, you accept these terms. If you do not accept them, do not use the software. Instead, contact the manufacturer or installer to determine its return policy. You must comply with that policy, which might limit your rights or require you to return the entire system on which the software is installed.
 * As described below, using the software also operates as your consent to the transmission of certain computer information during activation, validation and for Internet-based services.
 * If you comply with these license terms, you have the rights below for each license you acquire.
 * 1. OVERVIEW.
 * a. Software. The software includes desktop operating system software. This software does not include Windows Live services. Windows Live services are available from Microsoft under a separate agreement.
 * b. License Model. The software is licensed on a per copy per computer basis. A computer is a physical hardware system with an internal storage device capable of running the software. A hardware partition or blade is considered to be a separate computer.
 * 2. INSTALLATION AND USE RIGHTS.
 * a. One Copy per Computer. The software license is permanently assigned to the computer with which the software is distributed. That computer is the “licensed computer.”
 * ￼￼
 * b. Licensed Computer. You may use the software on up to two processors on the licensed computer at one time. Unless otherwise provided in these license terms, you may not use the software on any other computer.
 * c. Number of Users. Unless otherwise provided in these license terms, only one user may use the software at a time on the licensed computer.
 * d. Alternative Versions. The software may include more than one version, such as 32-bit and 64-bit. You may use only one version at one time. If the manufacturer or installer provides you with a one-time selection between language versions, you may use only the one language version you select.
 * 3. ADDITIONAL LICENSING REQUIREMENTS AND/OR USE RIGHTS.
 * a. Multiplexing. Hardware or software you use to
 * · pool connections, or
 * · reduce the number of devices or users that directly access or use the software
 * (sometimes referred to as “multiplexing” or “pooling”), does not reduce the number of licenses you need.
 * b. Font Components. While the software is running, you may use its fonts to display and print content. You may only
 * · embed fonts in content as permitted by the embedding restrictions in the fonts; and
 * · temporarily download them to a printer or other output device to print content.
 * c. Icons, Images and Sounds. While the software is running, you may use but not share its icons, images, sounds, and media. The sample images, sounds and media provided with the software are for your non-commercial use only.
 * d. Use with Virtualization Technologies. Instead of using the software directly on the licensed computer, you may install and use the software within only one virtual (or otherwise emulated) hardware system on the licensed computer. When used in a virtualized environment, content protected by digital rights management technology, BitLocker or any full volume disk drive encryption technology may not be as secure as protected content not in a virtualized environment. You should comply with all domestic and international laws that apply to such protected content.
 * e. Device Connections. You may allow up to 20 other devices to access software installed on the licensed computer to use only File Services, Print Services, Internet Information Services and Internet Connection Sharing and Telephony Services.
 * f. Remote Access Technologies You may access and use the software installed on the licensed computer remotely from another device using remote access technologies as follows.
 * · Remote Desktop. The single primary user of the licensed computer may access a session from any other device using Remote Desktop or similar technologies. A “session” means the experience of interacting with the software, directly or indirectly, through any combination of input, output and display peripherals. Other users may access a session from any device
 * ￼
 * using these technologies, if the remote device is separately licensed to run the software.
 * · Other Access Technologies. You may use Remote Assistance or similar technologies to share an active session.
 * g. Media Center Extender. You may have five Media Center Extender sessions (or other software or devices which provide similar functionality for a similar purpose) running at the same time to display the software user interface or content on other displays or devices.
 * h. Electronic Programming Guide. If the software includes access to an electronic programming guide service that displays customized television listings, a separate service agreement applies to the service. If you do not agree to the terms of the service agreement, you may continue to use the software, but you will not be able to use the electronic programming guide service. The service may contain advertising content and related data, which are received and stored by the software. The service is not available in all areas. Please consult the software information for instructions on accessing the service agreement.
 * i. Related Media Information. If you request related media information as part of your playback experience, the data provided to you may not be in your local language. Some countries or regions have laws and regulations which may restrict or limit your ability to access certain types of content.
 * j. Worldwide Use of the Media Center. Media Center is not designed for use in every country. For example, although the Media Center information may refer to certain features such as an electronic programming guide or provide information on how to configure a TV tuner, these features may not work in your area. Please refer to the Media Center information for a list of features that may not work in your area.
 * 4. MANDATORY ACTIVATION.
 * Activation associates the use of the software with a specific computer. During activation, the software will send information about the software and the computer to Microsoft. This information includes the version, language and product key of the software, the Internet protocol address of the computer, and information derived from the hardware configuration of the computer. For more information, see go.microsoft.com/fwlink/?Linkid=104609. By using the software, you consent to the transmission of this information. If properly licensed, you have the right to use the version of the software installed during the installation process up to the time permitted for activation. Unless the software is activated, you have no right to use the software after the time permitted for activation. This is to prevent its unlicensed use. You are not permitted to bypass or circumvent activation. If the computer is connected to the Internet, the software may automatically connect to Microsoft for activation. You can also activate the software manually by Internet or telephone. If you do so, Internet and telephone service charges may apply. Some changes to your computer components or the software may require you to reactivate the software. The software will remind you to activate it until you do.
 * 5. VALIDATION.
 * a. Validation verifies that the software has been activated and is properly licensed. It also verifies that no unauthorized changes have been made to the validation, licensing, or activation functions of the software. Validation may also check for certain malicious or unauthorized software related to such unauthorized changes. A validation check confirming that you are properly licensed permits you to continue to use the software, certain features of the software or to obtain additional benefits. You are not permitted to circumvent validation. This is to prevent
 * ￼
 * unlicensed use of the software. For more information, see go.microsoft.com/fwlink/?Linkid=104610.
 * b. The software will from time to time perform a validation check of the software. The check may be initiated by the software or Microsoft. To enable the activation function and validation checks, the software may from time to time require updates or additional downloads of the validation, licensing or activation functions of the software. The updates or downloads are required for the proper functioning of the software and may be downloaded and installed without further notice to you. During or after a validation check, the software may send information about the software, the computer and the results of the validation check to Microsoft. This information includes, for example, the version and product key of the software, any unauthorized changes made to the validation, licensing or activation functions of the software, any related malicious or unauthorized software found and the Internet protocol address of the computer. Microsoft does not use the information to identify or contact you. By using the software, you consent to the transmission of this information. For more information about validation and what is sent during or after a validation check, see go.microsoft.com/fwlink/?Linkid=104611.
 * c. If, after a validation check, the software is found to be counterfeit, improperly licensed, or a non-genuine Windows product, or if it includes unauthorized changes, then the functionality and experience of using the software will be affected. For example:
 * Microsoft may
 * · repair the software, and remove, quarantine or disable any unauthorized changes that may interfere with the proper use of the software, including circumvention of the activation or validation functions of the software; or
 * · check and remove malicious or unauthorized software known to be related to such unauthorized changes; or
 * · provide notice that the software is improperly licensed or a non-genuine Windows product; and you may
 * · receive reminders to obtain a properly licensed copy of the software; or
 * · need to follow Microsoft’s instructions to be licensed to use the software and reactivate; and you may not be able to
 * · use or continue to use the software or some of the features of the software; or
 * · obtain certain updates or upgrades from Microsoft.
 * d. You may only obtain updates or upgrades for the software from Microsoft or authorized sources. For more information on obtaining updates from authorized sources see go.microsoft.com/fwlink/?Linkid=104612.
 * 6. POTENTIALLY UNWANTED SOFTWARE. If turned on, Windows Defender will search your computer for “spyware,” “adware” and other potentially unwanted software. If it finds potentially unwanted software, the software will ask you if you want to ignore, disable (quarantine) or remove it. Any potentially unwanted software rated “high” or “severe,” will automatically be removed after scanning unless you change the default setting. Removing or disabling potentially unwanted software
 * may result in
 * · other software on your computer ceasing to work, or
 * · your breaching a license to use other software on your computer.
 * By using this software, it is possible that you will also remove or disable software that is not potentially unwanted software.
 * 7. INTERNET-BASED SERVICES. Microsoft provides Internet-based services with the software. It may change or cancel them at any time.
 * a. Consent for Internet-Based Services. The software features described below and in the Windows 7 Privacy Statement connect to Microsoft or service provider computer systems over the Internet. In some cases, you will not receive a separate notice when they connect. In some cases, you may switch off these features or not use them. For more information about these features, see the Windows 7 Privacy Statement at go.microsoft.com/fwlink/?linkid=104604. By using these features, you consent to the transmission of the information described below. Microsoft does not use the information to identify or contact you.
 * Computer Information. The following features use Internet protocols, which send to the appropriate systems computer information, such as your Internet protocol address, the type of operating system, browser and name and version of the software you are using, and the language code of the computer where you installed the software. Microsoft uses this information to make the Internet-based services available to you.
 * · Plug and Play and Plug and Play Extensions. You may connect new hardware to your computer, either directly or over a network. Your computer may not have the drivers needed to communicate with that hardware. If so, the update feature of the software can obtain the correct driver from Microsoft and install it on your computer. An administrator can disable this update feature.
 * · Windows Update. To enable the proper functioning of the Windows Update service in the software (if you use it), updates or downloads to the Windows Update service will be required from time to time and downloaded and installed without further notice to you.
 * · Web Content Features. Features in the software can retrieve related content from Microsoft and provide it to you. Examples of these features are clip art, templates, online training, online assistance and Appshelp. You may choose not to use these web content features.
 * · Digital Certificates. The software uses digital certificates. These digital certificates confirm the identity of Internet users sending X.509 standard encrypted information. They also can be used to digitally sign files and macros, to verify the integrity and origin of the file contents. The software retrieves certificates and updates certificate revocation lists over the Internet, when available.
 * · Auto Root Update. The Auto Root Update feature updates the list of trusted certificate authorities. You can switch off the Auto Root Update feature.
 * · Windows Media Digital Rights Management. Content owners use Windows Media digital rights management technology (WMDRM) to protect their intellectual property, including copyrights. This software and third party software use WMDRM to play and copy WMDRM-protected content. If the software fails to protect the content, content owners may
 * ￼￼￼￼￼￼￼
 * ask Microsoft to revoke the software’s ability to use WMDRM to play or copy protected content. Revocation does not affect other content. When you download licenses for protected content, you agree that Microsoft may include a revocation list with the licenses. Content owners may require you to upgrade WMDRM to access their content. Microsoft software that includes WMDRM will ask for your consent prior to the upgrade. If you decline an upgrade, you will not be able to access content that requires the upgrade. You may switch off WMDRM features that access the Internet. When these features are off, you can still play content for which you have a valid license.
 * · Windows Media Player. When you use Windows Media Player, it checks with Microsoft for
 * · compatible online music services in your region; and
 * · new versions of the player.
 * For more information, go to go.microsoft.com/fwlink/?linkid=104605.
 * · Malicious Software Removal. During setup, if you select “Get important updates for installation”, the software may check for and remove certain malware from your computer. “Malware” is malicious software. If the software runs, it will remove the Malware listed and updated at www.support.microsoft.com/?kbid=890830. During a Malware check, a report will be sent to Microsoft with specific information about Malware detected, errors, and other information about your computer. This information is used to improve the software and other Microsoft products and services. No information included in these reports will be used to identify or contact you. You may disable the software’s reporting functionality by following the instructions found at www.support.microsoft.com/?kbid=890830. For more information read the Windows Malicious Software Removal Tool privacy statement at go.microsoft.com/fwlink/?LinkId=113995.
 * · Network Awareness. This feature determines whether a system is connected to a network by either passive monitoring of network traffic or active DNS or HTTP queries. The query only transfers standard TCP/IP or DNS information for routing purposes. You can switch off the active query feature through a registry setting.
 * · Windows Time Service. This service synchronizes with time.windows.com once a week to provide your computer with the correct time. You can turn this feature off or choose your preferred time source within the Date and Time Control Panel applet. The connection uses the standard NTP protocol.
 * · IPv6 Network Address Translation (NAT) Traversal service (Teredo). This feature helps existing home Internet gateway devices transition to IPv6. IPv6 is the next generation Internet protocol. It helps enable end-to-end connectivity often needed by peer-to-peer applications. To do so, each time you start up the software the Teredo client service will attempt to locate a public Teredo Internet service. It does so by sending a query over the Internet. This query only transfers standard Domain Name Service information to determine if your computer is connected to the Internet and can locate a public Teredo service. If you
 * · use an application that needs IPv6 connectivity, or
 * · configure your firewall to always enable IPv6 connectivity,
 * Then, by default standard Internet Protocol information will be sent to the Teredo service at Microsoft at regular intervals. No other information is sent to Microsoft. You can change this
 * ￼￼￼￼￼
 * default to use non-Microsoft servers. You can also switch off this feature using a command line utility named “netsh”.
 * · Accelerators. When you click on or move your mouse over an Accelerator, in Internet Explorer, any of the following may be sent to the service provider:
 * · the title and full web address or URL of the current webpage,
 * · standard computer information, and
 * · any content you have selected.
 * If you use an Accelerator provided by Microsoft, use of the information sent is subject to the Microsoft Online Privacy Statement. This statement is available at go.microsoft.com/fwlink/?linkid=31493. If you use an Accelerator provided by a third party, use of the information sent will be subject to the third party’s privacy practices.
 * · Search Suggestions Service. In Internet Explorer, when you type a search query in the Instant Search box or type a question mark (?) before your search term in the Address bar, you will see search suggestions as you type (if supported by your search provider). Everything you type in the Instant Search box or in the Address bar when preceded by a question mark (?) is sent to your search provider as you type. Also, when you press Enter or click the Search button, the text in the Instant Search box or Address bar is sent to the search provider. If you use a Microsoft search provider, use of the information sent is subject to the Microsoft Online Privacy Statement. This statement is available at go.microsoft.com/fwlink/?linkid=31493. If you use a third-party search provider, use of the information sent will be subject to the third party’s privacy practices. You can turn search suggestions off at any time. To do so, use Manage Add-ons under the Tools button in Internet Explorer. For more information about the search suggestions service, see go.microsoft.com/fwlink/?linkid=128106.
 * · Consent to Update Infrared Emitter/Receiver. The software may contain technology to ensure the proper functioning of the infrared emitter/receiver device shipped with certain Media Center-based products. You agree that the software may update the firmware of this computer.
 * · Media Center Online Promotions. If you use Media Center features of the software to access Internet-based content or other Internet-based services, such services may obtain the following information from the software to enable you to receive, accept and use certain promotional offers:
 * · certain computer information, such as your Internet protocol address, the type of operating system and browser you are using, and the name and version of the software you are using,
 * · the requested content, and
 * · the language code of the computer where you installed the software.
 * Your use of the Media Center features to connect to those services serves as your consent to the collection and use of such information.
 * b. Use of Information. Microsoft may use the computer information, accelerator information,
 * ￼￼￼￼
 * search suggestions information, error reports, and Malware reports to improve our software and services. We may also share it with others, such as hardware and software vendors. They may use the information to improve how their products run with Microsoft software.
 * c. Misuse of Internet-based Services. You may not use these services in any way that could harm them or impair anyone else’s use of them. You may not use the services to try to gain unauthorized access to any service, data, account or network by any means.
 * 8. SCOPE OF LICENSE. The software is licensed, not sold. This agreement only gives you some rights to use the features included in the software edition you licensed. The manufacturer or installer and Microsoft reserve all other rights. Unless applicable law gives you more rights despite this limitation, you may use the software only as expressly permitted in this agreement. In doing so, you must comply with any technical limitations in the software that only allow you to use it in certain ways. You may not
 * · work around any technical limitations in the software;
 * · reverse engineer, decompile or disassemble the software, except and only to the extent that applicable law expressly permits, despite this limitation;
 * · use components of the software to run applications not running on the software;
 * · make more copies of the software than specified in this agreement or allowed by applicable law, despite this limitation;
 * · publish the software for others to copy;
 * · rent, lease or lend the software; or
 * · use the software for commercial software hosting services.
 * 9. MICROSOFT .NET BENCHMARK TESTING. The software includes one or more components of the .NET Framework (“.NET Components”). You may conduct internal benchmark testing of those components. You may disclose the results of any benchmark test of those components, provided that you comply with the conditions set forth at go.microsoft.com/fwlink/?LinkID=66406. Notwithstanding any other agreement you may have with Microsoft, if you disclose such benchmark test results, Microsoft shall have the right to disclose the results of benchmark tests it conducts of your products that compete with the applicable .NET Component, provided it complies with the same conditions set forth at go.microsoft.com/fwlink/?LinkID=66406.
 * 10. BACKUP COPY. You may make one backup copy of the software. You may use it only to reinstall the software on the licensed computer.
 * 11. DOCUMENTATION. Any person that has valid access to your computer or internal network may copy and use the documentation for your internal, reference purposes.
 * 12. NOT FOR RESALE SOFTWARE. You may not sell software marked as “NFR” or “Not for Resale.”
 * 13. GEOGRAPHIC RESTRICTIONS. If the software is marked as requiring activation in a specific geographic region, then you are only permitted to activate this software in the geographic region indicated on the software or computer packaging. You may not be able to activate the software outside of that region. For further information on geographic restrictions, visit go.microsoft.com/fwlink/?LinkId=141397.
 * 14. UPGRADES. To use upgrade software, you must first be licensed for the software that is eligible for the upgrade. Upon upgrade, this agreement takes the place of the agreement for the software you upgraded from. After you upgrade, you may no longer use the software you upgraded from.
 * 15. DOWNGRADE. Instead of using the software, you may use one of the following earlier versions:
 * · Windows Vista Business,
 * · Microsoft Windows XP Professional,
 * · Microsoft Windows Professional x64 Edition, or
 * · Microsoft Windows XP Tablet PC Edition.
 * This agreement applies to your use of the earlier versions. If the earlier version includes different components, any terms for those components in the agreement that comes with the earlier version apply to your use of them. Neither the manufacturer or installer, nor Microsoft is obligated to supply earlier versions to you. You must obtain the earlier version separately. At any time, you may replace an earlier version with this version of the software.
 * 16. PROOF OF LICENSE.
 * a. Genuine Proof of License. If you acquired the software on a computer, or on a disc or other media, a genuine Microsoft Certificate of Authenticity label with a genuine copy of the software identifies licensed software. To be valid, this label must be affixed to the computer or appear on the manufacturer’s or installer’s packaging. If you receive the label separately, it is invalid. You should keep label on the computer or the packaging that has the label on it to prove that you are licensed to use the software. If the computer comes with more than one genuine Certificate of Authenticity label, you may use each version of the software identified on those labels.
 * b. Windows Anytime Upgrade License. If you upgrade the software using Windows Anytime Upgrade, your proof of license is identified by
 * · the genuine Microsoft Certificate of Authenticity label for the software you upgraded from, and
 * · the genuine Microsoft proof of purchase label from the Windows Anytime Upgrade Kit you used to upgrade. Proof of purchase may be subject to verification by your merchant’s records.
 * c. To identify genuine Microsoft software, see www.howtotell.com.
 * 17. TRANSFER TO A THIRD PARTY. You may transfer the software directly to a third party only with the licensed computer. The transfer must include the software and the Certificate of Authenticity label. You may not keep any copies of the software or any earlier version. Before any permitted transfer, the other party must agree that this agreement applies to the transfer and use of the software.
 * 18. NOTICE ABOUT THE H.264/AVC VISUAL STANDARD, THE VC-1 VIDEO STANDARD, THE MPEG-4 VISUAL STANDARD AND THE MPEG-2 VIDEO STANDARD. This software includes H.264/AVC, VC-1, MPEG-4 Part 2, and MPEG-2 visual compression technology. MPEG LA, L.L.C. requires this notice:
 * THIS PRODUCT IS LICENSED UNDER THE AVC, THE VC-1, THE MPEG-4 PART 2 VISUAL, AND THE MPEG-2 VIDEO PATENT PORTFOLIO LICENSES FOR THE PERSONAL AND NON-COMMERCIAL USE OF A CONSUMER TO (i) ENCODE VIDEO IN COMPLIANCE WITH THE ABOVE STANDARDS (“VIDEO STANDARDS”) AND/OR (ii) DECODE AVC, VC-1, MPEG-4 PART 2 AND MPEG-2 VIDEO THAT WAS ENCODED BY A CONSUMER ENGAGED IN A PERSONAL AND NON-COMMERCIAL ACTIVITY OR WAS OBTAINED FROM A VIDEO PROVIDER LICENSED TO PROVIDE SUCH VIDEO. NONE OF THE LICENSES EXTEND TO ANY OTHER PRODUCT REGARDLESS OF WHETHER SUCH PRODUCT IS INCLUDED WITH THIS PRODUCT IN A SINGLE ARTICLE. NO LICENSE IS GRANTED OR SHALL BE IMPLIED FOR ANY OTHER USE. ADDITIONAL INFORMATION MAY BE OBTAINED FROM MPEG LA, L.L.C. SEE WWW.MPEGLA.COM.
 * 19. THIRD PARTY PROGRAMS. The software contains third party programs. The license terms with those programs apply to your use of them.
 * 20. EXPORT RESTRICTIONS. The software is subject to United States export laws and regulations. You must comply with all domestic and international export laws and regulations that apply to the software. These laws include restrictions on destinations, end users and end use. For additional information, see www.microsoft.com/exporting.
 * 21. SUPPORT SERVICES. For the software generally, contact the manufacturer or installer for support options. Refer to the support number provided with the software. For updates and supplements obtained directly from Microsoft, Microsoft provides support as described at www.support.microsoft.com/common/international.aspx. If you are using software that is not properly licensed, you will not be entitled to receive support services.
 * 22. ENTIRE AGREEMENT. This agreement (including the warranty below), additional terms (including any printed-paper license terms that accompany the software and may modify or replace some or all of these terms), and the terms for supplements, updates, Internet-based services and support services that you use, are the entire agreement for the software and support services.
 * 23. APPLICABLE LAW.
 * a. United States. If you acquired the software in the United States, Washington state law governs the interpretation of this agreement and applies to claims for breach of it, regardless of conflict of laws principles. The laws of the state where you live govern all other claims, including claims under state consumer protection laws, unfair competition laws, and in tort.
 * b. Outside the United States. If you acquired the software in any other country, the laws of that country apply.
 * 24. LEGAL EFFECT. This agreement describes certain legal rights. You may have other rights under the laws of your state or country. You may also have rights with respect to the party from whom you acquired the software. This agreement does not change your rights under the laws of your state or country if the laws of your state or country do not permit it to do so.
 * 25. LIMITATION ON AND EXCLUSION OF DAMAGES. Except for any refund the manufacturer or installer may provide, you cannot recover any other damages, including consequential, lost profits, special, indirect or incidental damages.
 * This limitation applies to
 * · anything related to the software, services, content (including code) on third party Internet sites, or third party programs; and
 * · claims for breach of contract, breach of warranty, guarantee or condition, strict liability, negligence, or other tort to the extent permitted by applicable law.
 * It also applies even if
 * · repair, replacement or a refund for the software does not fully co\n* mpensate you for any losses; or
 * · Microsoft knew or should have known about the possibility of the damages.
 * Some states do not allow the exclusion or limitation of incidental or consequential damages, so the above limitation or exclusion may not apply to you. They also may not apply to you because your country may not allow the exclusion or limitation of incidental, consequential or other damages.
 * *********************************************************************** LIMITED WARRANTY
 * A. LIMITED WARRANTY. If you follow the instructions and the software is properly licensed, the software will perform substantially as described in the Microsoft materials that you receive in or with the software.
 * B. TERM OF WARRANTY; WARRANTY RECIPIENT; LENGTH OF ANY IMPLIED WARRANTIES. The limited warranty covers the software for 90 days after acquired by the first user. If you receive supplements, updates, or replacement software during those 90 days, they will be covered for the remainder of the warranty or 30 days, whichever is longer. If you transfer the software, the remainder of the warranty will apply to the recipient.
 * To the extent permitted by law, any implied warranties, guarantees or conditions last only during the term of the limited warranty. Some states do not allow limitations on how long an implied warranty lasts, so these limitations may not apply to you. They also might not apply to you because some countries may not allow limitations on how long an implied warranty, guarantee or condition lasts.
 * C. EXCLUSIONS FROM WARRANTY. This warranty does not cover problems caused by your acts (or failures to act), the acts of others, or events beyond the reasonable control of the manufacturer or installer, or Microsoft.
 * D. REMEDY FOR BREACH OF WARRANTY. The manufacturer or installer will, at its election, either (i) repair or replace the software at no charge, or (ii) accept return of the product(s) for a refund of the amount paid, if any. The manufacturer or installer may also repair or replace supplements, updates and replacement software or provide a refund of the amount you paid for them, if any. contact the manufacturer or installer about its policy. These are your only remedies for breach of the limited warranty.
 * E. CONSUMER RIGHTS NOT AFFECTED. You may have additional consumer rights under your local laws, which this agreement cannot change.
 * F. WARRANTY PROCEDURES. Contact the manufacturer or installer to find out how to obtain warranty service for the software. For a refund, you must comply with the manufacturer’s or installer’s return policies.
 * G. NO OTHER WARRANTIES. The limited warranty is the only direct warranty from the manufacturer or installer, or Microsoft. The manufacturer or installer and Microsoft give no other express warranties, guarantees or conditions. Where allowed by your local laws, the manufacturer or installer and Microsoft exclude implied warranties of merchantability, fitness for a particular purpose and non-infringement. If your local laws give you any implied warranties, guarantees or conditions, despite this exclusion, your remedies are described in the Remedy for Breach of Warranty clause above, to the extent permitted by your local laws.
 * H. LIMITATION ON AND EXCLUSION OF DAMAGES FOR BREACH OF WARRANTY. The Limitation on and Exclusion of Damages clause above applies to breaches of this limited warranty.
 * This warranty gives you specific legal rights, and you may also have other rights which vary from state to state. You may also have other rights which vary from country to
 * country.
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

#define MS_HIDINPUT		0x01
#define MS_ERGONOMY		0x02
#define MS_PRESENTER		0x04
#define MS_RDESC		0x08
#define MS_NOGET		0x10
#define MS_DUPLICATE_USAGES	0x20

/*
 * Microsoft Wireless Desktop Receiver (Model 1028) has
 * 'Usage Min/Max' where it ought to have 'Physical Min/Max'
 */
static __u8 *ms_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if ((quirks & MS_RDESC) && *rsize == 571 && rdesc[557] == 0x19 &&
			rdesc[559] == 0x29) {
		hid_info(hdev, "fixing up Microsoft Wireless Receiver Model 1028 report descriptor\n");
		rdesc[557] = 0x35;
		rdesc[559] = 0x45;
	}
	return rdesc;
}

#define ms_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))
static int ms_ergonomy_kb_quirk(struct hid_input *hi, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct input_dev *input = hi->input;

	switch (usage->hid & HID_USAGE) {
	case 0xfd06: ms_map_key_clear(KEY_CHAT);	break;
	case 0xfd07: ms_map_key_clear(KEY_PHONE);	break;
	case 0xff05:
		set_bit(EV_REP, input->evbit);
		ms_map_key_clear(KEY_F13);
		set_bit(KEY_F14, input->keybit);
		set_bit(KEY_F15, input->keybit);
		set_bit(KEY_F16, input->keybit);
		set_bit(KEY_F17, input->keybit);
		set_bit(KEY_F18, input->keybit);
	default:
		return 0;
	}
	return 1;
}

static int ms_presenter_8k_quirk(struct hid_input *hi, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	set_bit(EV_REP, hi->input->evbit);
	switch (usage->hid & HID_USAGE) {
	case 0xfd08: ms_map_key_clear(KEY_FORWARD);	break;
	case 0xfd09: ms_map_key_clear(KEY_BACK);	break;
	case 0xfd0b: ms_map_key_clear(KEY_PLAYPAUSE);	break;
	case 0xfd0e: ms_map_key_clear(KEY_CLOSE);	break;
	case 0xfd0f: ms_map_key_clear(KEY_PLAY);	break;
	default:
		return 0;
	}
	return 1;
}

static int ms_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_MSVENDOR)
		return 0;

	if (quirks & MS_ERGONOMY) {
		int ret = ms_ergonomy_kb_quirk(hi, usage, bit, max);
		if (ret)
			return ret;
	}

	if ((quirks & MS_PRESENTER) &&
			ms_presenter_8k_quirk(hi, usage, bit, max))
		return 1;

	return 0;
}

static int ms_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if (quirks & MS_DUPLICATE_USAGES)
		clear_bit(usage->code, *bit);

	return 0;
}

static int ms_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	unsigned long quirks = (unsigned long)hid_get_drvdata(hdev);

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || !field->hidinput ||
			!usage->type)
		return 0;

	/* Handling MS keyboards special buttons */
	if (quirks & MS_ERGONOMY && usage->hid == (HID_UP_MSVENDOR | 0xff05)) {
		struct input_dev *input = field->hidinput->input;
		static unsigned int last_key = 0;
		unsigned int key = 0;
		switch (value) {
		case 0x01: key = KEY_F14; break;
		case 0x02: key = KEY_F15; break;
		case 0x04: key = KEY_F16; break;
		case 0x08: key = KEY_F17; break;
		case 0x10: key = KEY_F18; break;
		}
		if (key) {
			input_event(input, usage->type, key, 1);
			last_key = key;
		} else
			input_event(input, usage->type, last_key, 0);

		return 1;
	}

	return 0;
}

static int ms_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	unsigned long quirks = id->driver_data;
	int ret;

	hid_set_drvdata(hdev, (void *)quirks);

	if (quirks & MS_NOGET)
		hdev->quirks |= HID_QUIRK_NOGET;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT | ((quirks & MS_HIDINPUT) ?
				HID_CONNECT_HIDINPUT_FORCE : 0));
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	return 0;
err_free:
	return ret;
}

static const struct hid_device_id ms_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_SIDEWINDER_GV),
		.driver_data = MS_HIDINPUT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_NE4K),
		.driver_data = MS_ERGONOMY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_LK6K),
		.driver_data = MS_ERGONOMY | MS_RDESC },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_PRESENTER_8K_USB),
		.driver_data = MS_PRESENTER },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_DIGITAL_MEDIA_3K),
		.driver_data = MS_ERGONOMY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_WIRELESS_OPTICAL_DESKTOP_3_0),
		.driver_data = MS_NOGET },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_COMFORT_MOUSE_4500),
		.driver_data = MS_DUPLICATE_USAGES },

	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_MICROSOFT, USB_DEVICE_ID_MS_PRESENTER_8K_BT),
		.driver_data = MS_PRESENTER },
	{ }
};
MODULE_DEVICE_TABLE(hid, ms_devices);

static struct hid_driver ms_driver = {
	.name = "microsoft",
	.id_table = ms_devices,
	.report_fixup = ms_report_fixup,
	.input_mapping = ms_input_mapping,
	.input_mapped = ms_input_mapped,
	.event = ms_event,
	.probe = ms_probe,
};

static int __init ms_init(void)
{
	return hid_register_driver(&ms_driver);
}

static void __exit ms_exit(void)
{
	hid_unregister_driver(&ms_driver);
}

module_init(ms_init);
module_exit(ms_exit);
MODULE_LICENSE("GPL");
