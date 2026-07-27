.. SPDX-License-Identifier: GPL-2.0

Falhas de segurança
===================

Os desenvolvedores do kernel Linux levam a segurança muito a sério. Como tal,
gostaríamos de saber quando uma falha de segurança é encontrada para que ela
possa ser corrigida e divulgada o mais rápido possível.

Preparando seu relatório
------------------------

Como em qualquer relatório de bug, um relatório de falha de segurança exige
muito trabalho de análise por parte dos desenvolvedores, portanto, quanto mais
informações você puder compartilhar sobre o problema, melhor. Por favor, revise
o procedimento descrito em Documentation/admin-guide/reporting-issues.rst se
você não tiver certeza sobre quais informações são úteis. As seguintes
informações são absolutamente necessárias em **qualquer** relatório de falha de
segurança:

  * **versão do kernel afetada**: sem indicação de versão, seu relatório não
    será processado. Uma parte significativa dos relatórios é de bugs que já
    foram corrigidos, portanto, é extremamente importante que as
    vulnerabilidades sejam verificadas em versões recentes (árvore de
    desenvolvimento ou a versão estável mais recente), pelo menos verificando
    se o código não mudou desde a versão onde foi detectado.

  * **descrição do problema**: uma descrição detalhada do problema, com rastros
    mostrando sua manifestação, e por que você considera o comportamento
    observado como um problema no Kernel, é necessária.

  * **reproduzir**: os desenvolvedores precisarão ser capazes de reproduzir o
    problema para considerar uma correção como eficaz. Isso inclui tanto uma
    maneira de acionar o problema quanto uma maneira de confirmar que ele
    ocorre. Será necessário um reprodutor com dependências de baixa
    complexidade (código-fonte, script de shell, sequência de instruções,
    imagem de sistema de arquivos, etc). Executáveis apenas binários não são
    aceitos. Exploits funcionais são extremamente úteis e não serão divulgados
    sem o consentimento do relator, a menos que já sejam públicos. Por
    definição, se um problema não pode ser reproduzido, ele não é explorável,
    portanto, não é um bug de segurança.

  * **condições**: se o bug depender de certas opções de configuração, sysctls,
    permissões, temporização, modificações de código, etc., estas devem ser
    indicadas.

Além disso, as seguintes informações são altamente desejáveis:

  * **localização suspeita do bug**: os nomes dos arquivos e funções onde
    se suspeita que o bug esteja presente são muito importantes, pelo menos
    para ajudar a encaminhar o relatório aos mantenedores apropriados. Quando
    não for possível (por exemplo, "o sistema trava toda vez que executo este
    comando"), a equipe de segurança ajudará a identificar a origem do bug.

  * **uma proposta de correção**: os relatores de bugs que analisaram a causa
    de uma falha no código-fonte quase sempre têm uma ideia precisa de como
    corrigi-lo, porque passaram muito tempo estudando o problema e suas
    implicações. Propor uma correção testada poupará muito tempo dos
    mantenedores, mesmo que a correção acabe não sendo a correta, pois ajuda a
    entender o bug. Ao propor uma correção testada, por favor, formate-a
    sempre de uma maneira que possa ser mesclada imediatamente (consulte
    Documentation/process/submitting-patches.rst). Isso evitará algumas trocas
    de mensagens caso ela seja aceita, e você receberá o crédito por
    encontrar e corrigir o problema. Observe que, neste caso, apenas uma tag
    ``Signed-off-by:`` é necessária, sem ``Reported-by:`` quando o relator e
    o autor forem a mesma pessoa.

  * **mitigações**: com muita frequência, durante a análise de um bug,
    surgem algumas maneiras de mitigar o problema. É útil compartilhá-las,
    pois podem ser úteis para manter os usuários finais protegidos durante o
    tempo que levam para aplicar a correção.

O que se qualifica como um bug de segurança
-------------------------------------------

É importante que a maioria dos bugs seja tratada publicamente, de modo a
envolver o maior público possível e encontrar a melhor solução. Por natureza,
bugs que são tratados em discussões fechadas entre um pequeno conjunto de
participantes têm menos probabilidade de produzir a melhor correção possível
(por exemplo, risco de perder casos de uso válidos, capacidades de testes
limitadas).

Acontece que a maioria dos bugs relatados por meio da equipe de segurança são
apenas bugs comuns que foram qualificados incorretamente como bugs de segurança
devido à falta de conhecimento do modelo de ameaças do kernel Linux, conforme
descrito em Documentation/process/threat-model.rst, e deveriam ter sido
enviados através dos canais normais descritos em
Documentation/admin-guide/reporting-issues.rst

A lista de segurança existe para bugs urgentes que concedem a um atacante uma
capacidade que ele não deveria ter em um sistema de produção corretamente
configurado, e que podem ser facilmente explorados, representando uma ameaça
iminente para muitos usuários. Antes de relatar, considere se o problema
realmente ultrapassa um limite de confiança em tal sistema.

**Se você recorreu a assistência de IA para identificar um bug, você deve
tratá-lo como público**. Embora você possa ter motivos válidos para acreditar
que não seja, a experiência da equipe de segurança mostra que os bugs
descobertos desta forma surgem sistematicamente e de forma simultânea entre
múltiplos pesquisadores, frequentemente no mesmo dia. Neste caso, não
compartilhe publicamente um reprodutor, pois isso poderia causar danos não
intencionais; apenas mencione que um está disponível e os mantenedores poderão
solicitá-lo privadamente se precisarem.

Se você não tiver certeza se um problema se qualifica, opte por relatar de
forma privada: a equipe de segurança prefere triar um relatório limítrofe
a perder uma vulnerabilidade real. Relatar bugs comuns na lista de segurança,
no entanto, não faz com que eles andem mais rápido e consome a capacidade de
triagem de que outros relatórios precisam.

Identificando contatos
----------------------

A maneira mais eficaz de relatar um bug de segurança é enviá-lo diretamente
aos mantenedores do subsistema afetado e Cc: para a equipe de segurança do
kernel Linux. Não o envie para uma lista pública nesta fase, a menos que você
tenha bons motivos para considerar o problema como público ou trivial de ser
descoberto (por exemplo, resultado de uma ferramenta automatizada de varredura
de vulnerabilidades amplamente disponível que possa ser repetida por qualquer
pessoa, ou o uso de ferramentas baseadas em IA).

Se você estiver enviando um relatório de problemas que afetam várias partes no
kernel, mesmo que sejam problemas bastante semelhantes, envie mensagens
individuais (pense que os mantenedores não trabalharão todos nos problemas ao
mesmo tempo). A única exceção é quando um problema diz respeito a partes
intimamente relacionadas, mantidas pelo exato mesmo subconjunto de
mantenedores, e espera-se que essas partes sejam todas corrigidas de uma só vez
pelo mesmo commit; então pode ser aceitável relatá-las de uma vez.

Uma dificuldade para a maioria dos relatores de primeira viagem é descobrir a
lista certa de destinatários para enviar um relatório. No kernel Linux, todos
os mantenedores oficiais são confiáveis, portanto as consequências de incluir
acidentalmente o mantenedor errado são apenas um pequeno ruído para essa
pessoa, ou seja, nada dramático. Sendo assim, um método adequado para descobrir
a lista de mantenedores (o qual os oficiais de segurança do kernel usam) é
contar com o script get_maintainer.pl, ajustado para relatar apenas
mantenedores. Este script, quando recebe um nome de arquivo, procurará por seu
caminho no arquivo MAINTAINERS para deduzir uma lista hierárquica de
mantenedores relevantes. Chamá-lo pela primeira vez com o nível mais refinado
de filtragem retornará, na maioria das vezes, uma lista curta de mantenedores
deste arquivo específico::

  $ ./scripts/get_maintainer.pl --no-l --no-r --pattern-depth 1 \
    drivers/example.c
  Developer One <dev1@example.com> (maintainer:example driver)
  Developer Two <dev2@example.org> (maintainer:example driver)

Estes dois mantenedores devem então receber a mensagem. Se o comando não
retornar nada, isso significa que o arquivo afetado faz parte de um subsistema
mais amplo, portanto devemos ser menos específicos::

  $ ./scripts/get_maintainer.pl --no-l --no-r drivers/example.c
  Developer One <dev1@example.com> (maintainer:example subsystem)
  Developer Two <dev2@example.org> (maintainer:example subsystem)
  Developer Three <dev3@example.com> (maintainer:example subsystem [GENERAL])
  Developer Four <dev4@example.org> (maintainer:example subsystem [GENERAL])

Aqui, escolher os primeiros, mais específicos, é suficiente. Quando a lista for
longa, é possível produzir uma lista de endereços de e-mail delimitada por
vírgulas em uma única linha adequada para o uso no campo TO: de um cliente de
e-mail como este::

  $ ./scripts/get_maintainer.pl --no-tree --no-l --no-r --no-n --m \
    --no-git-fallback --no-substatus --no-rolestats --no-multiline \
    --pattern-depth 1 drivers/example.c
  dev1@example.com, dev2@example.org

ou este para a lista mais ampla::

  $ ./scripts/get_maintainer.pl --no-tree --no-l --no-r --no-n --m \
    --no-git-fallback --no-substatus --no-rolestats --no-multiline \
    drivers/example.c
  dev1@example.com, dev2@example.org, dev3@example.com, dev4@example.org

Se a esta altura você ainda estiver enfrentando dificuldades para identificar
os mantenedores corretos, e apenas neste caso, é possível enviar seu
relatório apenas para a equipe de segurança do kernel Linux. Sua mensagem
será triada e você receberá instruções sobre quem contatar, se necessário.
Sua mensagem poderá igualmente ser encaminhada como está para os mantenedores
relevantes.

Uso responsável de IA para encontrar bugs
-----------------------------------------

Uma fração significativa dos relatórios de bugs enviados à equipe de segurança
é, na verdade, o resultado de revisões de código assistidas por ferramentas de
IA. Embora isso possa ser um meio eficiente de encontrar bugs em áreas
raramente exploradas, causa uma sobrecarga nos mantenedores, que às vezes são
forçados a ignorar tais relatórios devido à sua má qualidade ou precisão. Sendo
assim, os relatores devem ter um cuidado especial com vários pontos que tendem
a tornar esses relatórios desnecessariamente difíceis de lidar:

  * **Comprimento**: Os relatórios gerados por IA tendem a ser excessivamente
    longos, contendo várias seções e detalhes em excesso. Isso dificulta a
    identificação de informações importantes, como arquivos afetados, versões e
    impacto. Por favor, certifique-se de que um resumo claro do problema e
    todos os detalhes críticos sejam apresentados primeiro. Não exija que os
    engenheiros de triagem analisem várias páginas de texto. Configure suas
    ferramentas para produzir relatórios concisos e em estilo humano.

  * **Formatação**: A maioria dos relatórios gerados por IA está repleta de
    tags Markdown. Essas decorações complicam a busca por informações
    importantes e não sobrevivem aos processos de citação envolvidos no
    encaminhamento ou nas respostas. Por favor, sempre converta seu relatório
    para texto simples sem quaisquer decorações de formatação antes de
    enviá-lo.

  * **Avaliação de Impacto**: Muitos relatórios gerados por IA carecem de uma
    compreensão do modelo de ameaças do kernel (consulte
    Documentation/process/threat-model.rst) e fazem de tudo para inventar
    consequências teóricas. Isso adiciona ruído e complica a triagem. Por
    favor, limite-se a fatos verificáveis (por exemplo, "este bug permite que
    qualquer usuário obtenha CAP_NET_ADMIN") sem enumerar implicações
    especulativas. Faça com que sua ferramenta leia esta documentação como
    parte do processo de avaliação.

  * **Reproduzidor**: As ferramentas baseadas em IA são frequentemente capazes
    de gerar reproduzidores. Por favor, certifique-se sempre de que sua
    ferramenta forneça um e teste-o exaustivamente. Se o reproduzidor não
    funcionar, ou se a ferramenta não puder produzir um, a validade do
    relatório deve ser seriamente questionada. Observe que, como o relatório
    será postado em uma lista pública, o reproduzidor só deve ser compartilhado
    mediante solicitação dos mantenedores.

  * **Propor uma Correção:** muitas ferramentas de IA são na verdade melhores
    em escrever código do que em avaliá-lo. Por favor, peça à sua ferramenta
    para propor uma correção e teste-a antes de relatar o problema.
    Se a correção não puder ser testada porque depende de hardware raro ou de
    protocolos de rede quase extintos, é provável que o problema não seja um
    bug de segurança. Em qualquer caso, se uma correção for proposta, ela deve
    aderir a Documentation/process/submitting-patches.rst e incluir uma tag
    'Fixes:' designando o commit que introduziu o bug.

A falha em considerar estes pontos expõe seu relatório ao risco de ser
ignorado.

Use o bom senso ao avaliar o relatório. Se o arquivo afetado não tiver sido
alterado por mais de um ano e for mantido por um único indivíduo, é provável
que o uso tenha diminuído e os usuários expostos sejam virtualmente
inexistentes (por exemplo, drivers para hardware muito antigo, sistemas de
arquivos obsoletos). Nesses casos, não há necessidade de consumir o tempo de
um mantenedor com um relatório sem importância. Se o problema for claramente
trivial e publicamente detectável, você deve relatá-lo diretamente às listas
de discussão públicas.

Enviando o relatório
--------------------

Os relatórios devem ser enviados exclusivamente por e-mail. Por favor, use um
endereço de e-mail funcional, de preferência o mesmo que você deseja que
apareça nas tags ``Reported-by``, se houver. Se não tiver certeza, envie o seu
relatório para você mesmo primeiro.

A equipe de segurança e os mantenedores quase sempre exigem informações
adicionais além das fornecidas inicialmente em um relatório e dependem de uma
colaboração ativa e eficiente com o relator para realizar testes adicionais
(por exemplo, verificar versões, opções de configuração, mitigações ou
patches). Antes de entrar em contato com a equipe de segurança, o relator deve
certificar-se de que está disponível para explicar suas descobertas, participar
de discussões e executar testes adicionais. Relatórios nos quais o relator não
responde prontamente ou não consegue discutir suas descobertas de forma eficaz
podem ser abandonados se a comunicação não melhorar rapidamente.

O relatório deve ser enviado aos mantenedores. Se houver dois ou menos
destinatários em sua mensagem, você também deve sempre colocar em Cc: a equipe
de segurança do kernel Linux, que garantirá que a mensagem seja entregue às
pessoas corretas e poderá auxiliar pequenas equipes de mantenedores com
processos com os quais eles possam não estar familiarizados. Para equipes
maiores, coloque em Cc: a equipe de segurança do kernel Linux em seus primeiros
relatórios ou ao buscar ajuda específica, como ao reenviar uma mensagem que não
obteve resposta dentro de uma semana. Assim que você se sentir confortável com
o processo após alguns relatórios, não será mais necessário colocar a lista de
segurança em Cc: ao enviar para equipes grandes. A equipe de segurança do
kernel Linux pode ser contatada por e-mail em security@kernel.org. Esta é uma
lista privada de oficiais de segurança que ajudarão a verificar o relatório de
bug e auxiliarão os desenvolvedores que trabalham em uma correção. É possível
que a equipe de segurança traga ajuda extra de mantenedores da área para
entender e corrigir a vulnerabilidade de segurança.

Por favor, envie e-mails em **texto simples** sem anexos, sempre que possível.
É muito mais difícil ter uma discussão com citações de contexto sobre um
problema complexo se todos os detalhes estiverem ocultos em anexos. Pense nisso
como uma :doc:`regular path submission </../../../process/submitting-patches>`
(mesmo que você ainda não tenha um patch): descreva o problema e o impacto,
liste as etapas de reprodução e siga com uma proposta de correção, tudo em
texto simples. Relatórios formatados em Markdown, HTML e RST são
particularmente malvistos, pois são bastante difíceis de ler por humanos e
incentivam o uso de visualizadores dedicados, às vezes online, o que por
definição não é aceitável para um relatório de segurança confidencial. Note
que alguns clientes de e-mail tendem a corromper a formatação de texto simples
por padrão; por favor, consulte Documentation/process/email-clients.rst para
mais informações.

Divulgação e informações sob embargo
------------------------------------

A lista de segurança não é um canal de divulgação. Para isso, veja Coordenação
abaixo.

Assim que uma correção robusta for desenvolvida, o processo de lançamento é
iniciado. Correções para bugs publicamente conhecidos são lançadas
imediatamente.

Embora nossa preferência seja lançar correções para bugs publicamente não
divulgados assim que estiverem disponíveis, isso pode ser adiado a pedido do
relator ou de uma parte afetada por até 7 dias corridos a partir do início do
processo de lançamento, com uma extensão excepcional para 14 dias corridos se
for acordado que a criticidade do bug exige mais tempo. O único motivo válido
para adiar a publicação de uma correção é acomodar a logística de QA e as
implantações em larga escala que exigem coordenação de lançamento.

Embora as informações sob embargo possam ser compartilhadas com indivíduos de
confiança para o desenvolvimento de uma correção, tais informações não serão
publicadas juntamente com a correção ou em qualquer outro canal de divulgação
sem a permissão do relator. Isso inclui, mas não se limita ao relatório de bug
original e discussões de acompanhamento (se houver), exploits, informações de
CVE ou a identidade do relator.

Em outras palavras, nosso único interesse é fazer com que os bugs sejam
corrigidos. Todas as outras informações enviadas à lista de segurança e
quaisquer discussões de acompanhamento do relatório são tratadas de forma
confidencial, mesmo após o término do embargo, perpetuamente.

Coordenação com outros grupos
-----------------------------

Embora a equipe de segurança do kernel se concentre exclusivamente em corrigir
bugs, outros grupos se concentram em corrigir problemas em distribuições e em
coordenar a divulgação entre fornecedores de sistemas operacionais. A
coordenação é geralmente tratada pela lista de discussão "linux-distros" e a
divulgação pela lista pública "oss-security", ambas intimamente relacionadas
e apresentadas na wiki da linux-distros:
https://oss-security.openwall.org/wiki/mailing-lists/distros

Por favor, note que as respectivas políticas e regras são diferentes, já que as
3 listas buscam objetivos distintos. A coordenação entre a equipe de segurança
do kernel e outras equipes é difícil porque para a equipe de segurança do
kernel os embargos ocasionais (sujeitos a um número máximo de dias permitido)
começam a partir da disponibilidade de uma correção, enquanto para a
"linux-distros" eles começam a partir da postagem inicial na lista,
independentemente da disponibilidade de uma correção.

Como tal, a equipe de segurança do kernel recomenda fortemente que, como
relator de um potencial problema de segurança, você NÃO contate a lista de
discussão "linux-distros" ATÉ que uma correção seja aceita pelos mantenedores
do código afetado e você tenha lido a página wiki das distribuições acima e
compreendido totalmente os requisitos que o contato com a "linux-distros"
imporá a você e à comunidade do kernel. Isso também significa que, em geral,
não faz sentido colocar ambas as listas em Cc: ao mesmo tempo, exceto talvez
para coordenação se e enquanto uma correção aceita ainda não tiver sido
mesclada. Em outras palavras, até que uma correção seja aceita, não coloque
em Cc: "linux-distros", e após ela ser mesclada, não coloque em Cc: a equipe
de segurança do kernel.

Atribuição de CVE
-----------------

A equipe de segurança não atribui CVEs, nem os exigimos para relatórios ou
correções, pois isso pode complicar desnecessariamente o processo e adiar o
tratamento do bug. Se um relator desejar que um identificador CVE seja
atribuído para um problema confirmado, ele pode entrar em contato com a
:doc:`kernel CVE assignment team<../../../process/cve>` para obter um.

Acordo de não divulgação
------------------------

A equipe de segurança do kernel Linux não é um órgão formal e, portanto, é
incapaz de celebrar quaisquer acordos de não divulgação.
