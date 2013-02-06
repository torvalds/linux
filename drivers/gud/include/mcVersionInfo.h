/** @addtogroup MC_RTM
 * @{
 * MobiCore Version Information
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 */

#ifndef MCVERSIONINFO_H_
#define MCVERSIONINFO_H_

/** Length of MobiCore product ID string. */
#define MC_PRODUCT_ID_LEN 64

/** Global MobiCore Version Information.
 */
typedef struct {
    char productId[MC_PRODUCT_ID_LEN]; /** < Product ID of Mobicore; zero-terminated */
    uint32_t versionMci;               /** < Version of Mobicore Control Interface */
    uint32_t versionSo;                /** < Version of Secure Objects */
    uint32_t versionMclf;              /** < Version of MobiCore Load Format */
    uint32_t versionContainer;         /** < Version of MobiCore Container Format */
    uint32_t versionMcConfig;          /** < Version of MobiCore Configuration Block Format */
    uint32_t versionTlApi;             /** < Version of MobiCore Trustlet API Implementation */
    uint32_t versionDrApi;             /** < Version of MobiCore Driver API Implementation */
    uint32_t versionCmp;               /** < Version of Content Management Protocol */
} mcVersionInfo_t;

#endif /** MCVERSIONINFO_H_ */
